// membarriertest: checks the membarrier() private expedited commands as ART uses them
// (MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED in its fault handler, PRIVATE_EXPEDITED for
// class visibility, PRIVATE_EXPEDITED_SYNC_CORE after writing JIT code). Prints PASS/FAIL
// per case and exits 1 on any failure. Run from the adb shell.
//
// The ordering case is the store-buffering test membarrier exists for: thread A stores x
// and loads y with only a compiler barrier between them, thread B stores y, calls
// membarrier() and loads x. Both loads seeing 0 is forbidden. The same run with B using a
// compiler barrier only is printed as a control: it shows the CPUs reorder without it.
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
  CMD_QUERY = 0,
  CMD_GLOBAL = 1 << 0,
  CMD_GLOBAL_EXPEDITED = 1 << 1,
  CMD_REGISTER_GLOBAL_EXPEDITED = 1 << 2,
  CMD_PRIVATE_EXPEDITED = 1 << 3,
  CMD_REGISTER_PRIVATE_EXPEDITED = 1 << 4,
  CMD_PRIVATE_EXPEDITED_SYNC_CORE = 1 << 5,
  CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE = 1 << 6,
};

static int fails;

static void check(int ok, const char* name, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void check(int ok, const char* name, const char* fmt, ...) {
  va_list ap;
  printf("%s %s: ", ok ? "PASS" : "FAIL", name);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
  if (!ok) fails++;
}

static int mb(int cmd, int flags) { return syscall(__NR_membarrier, cmd, flags); }

// Expect a return value (want >= 0) or a failure with errno want_err.
static void expect(const char* name, int cmd, int flags, int want, int want_err) {
  errno = 0;
  int r = mb(cmd, flags);
  int e = errno;
  if (want_err)
    check(r == -1 && e == want_err, name, "returned %d errno %d (%s); want -1 errno %d", r, e,
          strerror(e), want_err);
  else
    check(r == want, name, "returned %d errno %d; want %d", r, e, want);
}

// --- store-buffering ordering test
static volatile int x, y, r1, r2;
static atomic_int go, done;
static int iters, use_mb, cmd_b;

static void pin(int cpu) {
  cpu_set_t s;
  CPU_ZERO(&s);
  CPU_SET(cpu, &s);
  sched_setaffinity(0, sizeof(s), &s);
}

static void* thread_a(void* arg) {
  pin(1);
  for (int i = 1; i <= iters; i++) {
    while (atomic_load_explicit(&go, memory_order_acquire) != i) {}
    x = 1;
    __asm__ __volatile__("" ::: "memory");  // compiler barrier only
    r1 = y;
    atomic_fetch_add_explicit(&done, 1, memory_order_release);
  }
  return arg;
}

static void* thread_b(void* arg) {
  pin(2);
  for (int i = 1; i <= iters; i++) {
    while (atomic_load_explicit(&go, memory_order_acquire) != i) {}
    y = 1;
    if (use_mb) mb(cmd_b, 0);
    else __asm__ __volatile__("" ::: "memory");
    r2 = x;
    atomic_fetch_add_explicit(&done, 1, memory_order_release);
  }
  return arg;
}

static int run_sb(int n, int with_mb, int cmd) {  // returns forbidden outcomes seen
  pthread_t a, b;
  int bad = 0;
  iters = n; use_mb = with_mb; cmd_b = cmd;
  atomic_store(&go, 0); atomic_store(&done, 0);
  pthread_create(&a, NULL, thread_a, NULL);
  pthread_create(&b, NULL, thread_b, NULL);
  for (int i = 1; i <= n; i++) {
    x = 0; y = 0; r1 = -1; r2 = -1;
    atomic_store_explicit(&done, 0, memory_order_relaxed);
    atomic_store_explicit(&go, i, memory_order_release);
    while (atomic_load_explicit(&done, memory_order_acquire) != 2) {}
    if (r1 == 0 && r2 == 0) bad++;
  }
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  return bad;
}

static void* spinner(void* arg) {  // a sibling thread kept running on another CPU
  pin((int)(long)arg);
  for (;;) __asm__ __volatile__("" ::: "memory");
  return NULL;
}

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "--after-exec")) {
    // Registration is cleared by execve.
    errno = 0;
    int r = mb(CMD_PRIVATE_EXPEDITED, 0);
    return (r == -1 && errno == EPERM) ? 0 : 1;
  }
  if (sysconf(_SC_NPROCESSORS_ONLN) < 3) {
    printf("needs 3 CPUs online\n0 failed\n");
    return 1;
  }

  int q = mb(CMD_QUERY, 0);
  check(q == 0x79, "query", "0x%x; want 0x79 (GLOBAL, PRIVATE_EXPEDITED, SYNC_CORE and their registrations)", q);
  expect("private expedited before registration", CMD_PRIVATE_EXPEDITED, 0, 0, EPERM);
  expect("sync core before registration", CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0, EPERM);
  expect("global expedited (not backported)", CMD_GLOBAL_EXPEDITED, 0, 0, EINVAL);
  expect("register global expedited (not backported)", CMD_REGISTER_GLOBAL_EXPEDITED, 0, 0, EINVAL);
  expect("unknown command", 1 << 7, 0, 0, EINVAL);
  expect("nonzero flags", CMD_QUERY, 1, 0, EINVAL);
  expect("global", CMD_GLOBAL, 0, 0, 0);

  // Register with another thread of this process running, so the kernel takes the
  // synchronize_sched() path rather than the single-thread one.
  pthread_t sp;
  pthread_create(&sp, NULL, spinner, (void*)3L);
  expect("register private expedited", CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0, 0);
  expect("register private expedited again", CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0, 0);
  expect("private expedited", CMD_PRIVATE_EXPEDITED, 0, 0, 0);
  expect("sync core before its own registration", CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0, EPERM);
  expect("register sync core", CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0, 0);
  expect("sync core", CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0, 0);

  int ok = 1;
  for (int i = 0; i < 2000 && ok; i++)
    ok = mb(CMD_PRIVATE_EXPEDITED, 0) == 0 && mb(CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0) == 0;
  check(ok, "2000 rounds of both commands", "%s", ok ? "all returned 0" : strerror(errno));

  // fork keeps the registration (the mm is copied); exec clears it.
  pid_t c = fork();
  if (c == 0) _exit(mb(CMD_PRIVATE_EXPEDITED, 0) == 0 ? 0 : 1);
  int st = 0;
  waitpid(c, &st, 0);
  check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "registration kept across fork", "child status 0x%x", st);
  c = fork();
  if (c == 0) {
    execl("/proc/self/exe", argv[0], "--after-exec", (char*)NULL);
    _exit(2);
  }
  waitpid(c, &st, 0);
  check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "registration cleared by exec",
        "child status 0x%x (0: EPERM after exec)", st);

  const int n = 200000;
  int control = run_sb(n, 0, 0);
  printf("INFO store buffering without membarrier: %d of %d rounds both loads saw 0\n", control, n);
  int bad = run_sb(n, 1, CMD_PRIVATE_EXPEDITED);
  check(bad == 0, "ordering with private expedited", "%d of %d rounds both loads saw 0", bad, n);
  bad = run_sb(n, 1, CMD_PRIVATE_EXPEDITED_SYNC_CORE);
  check(bad == 0, "ordering with sync core", "%d of %d rounds both loads saw 0", bad, n);

  printf("%d failed\n", fails);
  return fails ? 1 : 0;
}
