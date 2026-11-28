// binderfreezetest: freezes and thaws a process that waits in BINDER_WRITE_READ, the way Android
// freezes a cached app, and checks the errors the ioctl returns: EINTR is fine (libbinder retries
// it), anything else is a failure. Before upstream 95c16f9d9cf9 ("binder: use EINTR for
// interrupted wait for work") the wait returned -ERESTARTSYS, which reaches user space as -512
// when the freezer's wakeup is gone by the time the syscall returns; libbinder then keeps a
// command the driver already took and resends it (com.google.android.gms.unstable aborted with
// "Driver did not consume write buffer", 2026-11-27). Prints PASS/FAIL and exits 1 on failure.
// Run as root from the adb shell.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/android/binder.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CG "/sys/fs/cgroup/binderfreezetest"
#define ROUNDS 3000

static volatile sig_atomic_t stop;
static void on_usr1(int sig) { (void)sig; stop = 1; }

static int put(const char* path, const char* s) {
  int fd = open(path, O_WRONLY);
  if (fd < 0) return -1;
  int r = write(fd, s, strlen(s)) == (ssize_t)strlen(s) ? 0 : -1;
  close(fd);
  return r;
}

// The child. Its looper thread waits for process work in a loop; a second, non-looper thread
// keeps giving it some: requesting and clearing a death notification on handle 0 queues
// BR_CLEAR_DEATH_NOTIFICATION_DONE on the process. So the looper is often running inside the
// ioctl, between waits, when a freeze comes, as libbinder's thread is after sending a call. (A
// thread already asleep in the wait is skipped by the freezer and never returns.) The looper
// counts each ioctl error by errno.
static int bfd;

static void* poker(void* arg) {
  struct { uint32_t c1; struct binder_handle_cookie hc1; uint32_t c2; struct binder_handle_cookie hc2; }
      __attribute__((packed)) w = {BC_REQUEST_DEATH_NOTIFICATION, {0, 0x1234},
                                   BC_CLEAR_DEATH_NOTIFICATION, {0, 0x1234}};
  while (!stop) {
    struct binder_write_read bwr = {.write_size = sizeof(w), .write_buffer = (uintptr_t)&w};
    ioctl(bfd, BINDER_WRITE_READ, &bwr);
    usleep(50);
  }
  return arg;
}

static int child(int out) {
  bfd = open("/dev/binder", O_RDWR | O_CLOEXEC);
  if (bfd < 0 || mmap(NULL, 128 * 1024, PROT_READ, MAP_PRIVATE, bfd, 0) == MAP_FAILED) return 2;
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_usr1;  // no SA_RESTART: the stop signal ends the wait with EINTR
  sigaction(SIGUSR1, &sa, NULL);
  // A reference to handle 0 (the context manager), then this thread becomes the looper.
  struct { uint32_t c1; uint32_t h; uint32_t c2; } __attribute__((packed)) w = {BC_INCREFS, 0, BC_ENTER_LOOPER};
  struct binder_write_read bwr = {.write_size = sizeof(w), .write_buffer = (uintptr_t)&w};
  if (ioctl(bfd, BINDER_WRITE_READ, &bwr) < 0) return 3;
  // SIGUSR1 goes to this thread only: the poker blocks it.
  sigset_t m;
  sigemptyset(&m);
  sigaddset(&m, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &m, NULL);
  pthread_t t;
  pthread_create(&t, NULL, poker, NULL);
  pthread_sigmask(SIG_UNBLOCK, &m, NULL);
  long eintr = 0, other = 0, ok = 0, other_errno = 0;
  uint32_t buf[64];
  while (!stop) {
    memset(&bwr, 0, sizeof(bwr));
    bwr.read_size = sizeof(buf);
    bwr.read_buffer = (uintptr_t)buf;
    if (ioctl(bfd, BINDER_WRITE_READ, &bwr) == 0) ok++;
    else if (errno == EINTR) eintr++;
    else { other++; other_errno = errno; }
  }
  pthread_join(t, NULL);
  dprintf(out, "%ld %ld %ld %ld\n", ok, eintr, other, other_errno);
  return 0;
}

int main(void) {
  int p[2];
  if (pipe(p)) return 1;
  rmdir(CG);
  if (mkdir(CG, 0755) && errno != EEXIST) { printf("FAIL mkdir %s: %s\n1 failed\n", CG, strerror(errno)); return 1; }
  pid_t c = fork();
  if (c == 0) { close(p[0]); _exit(child(p[1])); }
  close(p[1]);
  char s[32];
  snprintf(s, sizeof(s), "%d", c);
  if (put(CG "/cgroup.procs", s)) { printf("FAIL move child into %s: %s\n1 failed\n", CG, strerror(errno)); kill(c, SIGKILL); return 1; }
  usleep(200000);  // let it reach the wait
  struct timespec ts = {0, 2000000};
  int n = 0;
  for (; n < ROUNDS; n++) {
    if (put(CG "/cgroup.freeze", "1")) break;
    nanosleep(&ts, NULL);
    if (put(CG "/cgroup.freeze", "0")) break;
    nanosleep(&ts, NULL);
  }
  put(CG "/cgroup.freeze", "0");
  usleep(100000);
  kill(c, SIGUSR1);
  long ok = -1, eintr = -1, other = -1, oe = 0;
  FILE* f = fdopen(p[0], "r");
  int got = fscanf(f, "%ld %ld %ld %ld", &ok, &eintr, &other, &oe);
  int st = 0;
  waitpid(c, &st, 0);
  rmdir(CG);
  int fails = 0;
  if (got != 4) {
    printf("FAIL child: no result (status 0x%x)\n", st);
    fails++;
  } else {
    printf("INFO %d freeze/thaw rounds; ioctl returned 0 %ld times, EINTR %ld times\n", n, ok, eintr);
    if (other) {
      printf("FAIL wait interrupted by the freezer: %ld returns with errno %ld (%s)\n", other, oe,
             oe == 512 ? "ERESTARTSYS, a kernel-internal code" : strerror(oe));
      fails++;
    } else printf("PASS wait interrupted by the freezer: no errno other than EINTR\n");
  }
  if (n < ROUNDS) { printf("FAIL cgroup.freeze: stopped after %d rounds: %s\n", n, strerror(errno)); fails++; }
  printf("%d failed\n", fails);
  return fails ? 1 : 0;
}
