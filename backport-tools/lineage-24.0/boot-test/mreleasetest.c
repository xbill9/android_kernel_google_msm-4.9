// process_mrelease(2) as lmkd's reaper uses it: kill a process through its pidfd, then release
// its memory before it finishes exiting. Error checks, then a race loop of kill + reap.
// Exit 0 when every check passes.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __NR_process_mrelease
#define __NR_process_mrelease 448
#endif
#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#endif
#ifndef __NR_pidfd_send_signal
#define __NR_pidfd_send_signal 424
#endif

static int passed, failed;
static void check(int ok, const char *what) {
  if (ok) passed++; else { failed++; printf("FAIL %s (errno %d %s)\n", what, errno, strerror(errno)); }
}
static int mrelease(int fd, unsigned f) { return syscall(__NR_process_mrelease, fd, f); }
static int pidfd_open(pid_t p) { return syscall(__NR_pidfd_open, p, 0); }
static int pidfd_kill(int fd) { return syscall(__NR_pidfd_send_signal, fd, SIGKILL, NULL, 0); }

// child: touch mb MiB of anonymous memory, tell the parent, then wait to be killed
static pid_t spawn(size_t mb, int *pfd) {
  int p[2]; if (pipe(p)) exit(2);
  pid_t c = fork();
  if (c == 0) {
    close(p[0]);
    char *m = mmap(NULL, mb << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) _exit(3);
    for (size_t i = 0; i < mb << 20; i += 4096) m[i] = 1;
    write(p[1], "r", 1);
    for (;;) pause();
  }
  close(p[1]);
  char b; read(p[0], &b, 1); close(p[0]);
  *pfd = pidfd_open(c);
  return c;
}

int main(int argc, char **argv) {
  int rounds = argc > 1 ? atoi(argv[1]) : 200;
  setvbuf(stdout, NULL, _IOLBF, 0);

  errno = 0;
  check(mrelease(-1, 0) == -1 && errno == EBADF, "bad fd -> EBADF (lmkd's probe: not ENOSYS)");
  int nf = open("/dev/null", O_RDONLY);
  errno = 0;
  check(mrelease(nf, 0) == -1 && errno == EBADF, "fd that is not a pidfd -> EBADF");
  close(nf);

  int fd; pid_t c = spawn(8, &fd);
  check(fd >= 0, "pidfd_open");
  errno = 0;
  check(mrelease(fd, 1) == -1 && errno == EINVAL, "unknown flag -> EINVAL");
  errno = 0;
  check(mrelease(fd, 0) == -1 && errno == EINVAL, "process that is not dying -> EINVAL");
  check(pidfd_kill(fd) == 0, "pidfd_send_signal SIGKILL");
  int r = mrelease(fd, 0);
  check(r == 0 || errno == ESRCH, "dying process -> 0 (or ESRCH once it is gone)");
  waitpid(c, NULL, 0);
  errno = 0;
  check(mrelease(fd, 0) == -1 && errno == ESRCH, "reaped process -> ESRCH");
  close(fd);

  // race loop: kill and reap at once, sizes 16 to 256 MiB
  int reaped = 0, gone = 0, other = 0;
  for (int i = 0; i < rounds; i++) {
    size_t mb = 16 << (i % 5);
    c = spawn(mb, &fd);
    pidfd_kill(fd);
    if (mrelease(fd, 0) == 0) reaped++;
    else if (errno == ESRCH) gone++;
    else { other++; if (other <= 3) printf("round %d: errno %d %s\n", i, errno, strerror(errno)); }
    int st; waitpid(c, &st, 0);
    if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGKILL) other++;
    close(fd);
  }
  printf("race loop: %d rounds, %d reaped, %d already gone, %d other\n", rounds, reaped, gone, other);
  check(other == 0, "race loop: only 0 or ESRCH, every child killed by SIGKILL");
  check(reaped > 0, "race loop: at least one reap before exit");

  printf("mreleasetest: %d passed, %d failed\n", passed, failed);
  return failed != 0;
}
