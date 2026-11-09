// clone3test: checks the clone3() and CAP_PERFMON/CAP_BPF/CAP_CHECKPOINT_RESTORE
// backports from the adb shell. Prints PASS/FAIL per case and exits 1 on any failure.
// Builds for arm64 and for 32-bit arm (the compat syscall table); the
// stack and TLS checks run in assembly on arm64 only.
#include <errno.h>
#include <linux/sched.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __NR_clone3
#define __NR_clone3 435
#endif
#ifndef CLONE_PIDFD
#define CLONE_PIDFD 0x00001000
#endif

struct args88 {  // clone_args as current uapi headers define it (88 bytes)
  uint64_t flags, pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls;
  uint64_t set_tid, set_tid_size, cgroup;
};

static int fails;
static void check(int ok, const char* name, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static void check(int ok, const char* name, const char* fmt, ...) {
  va_list ap;
  printf("%s %s: ", ok ? "PASS" : "FAIL", name);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
  if (!ok) fails++;
}

static long clone3_raw(void* a, size_t size) { return syscall(__NR_clone3, a, size); }

// clone3 that must fail: returns errno, or reaps the child and returns 0 if it succeeded.
static int clone3_err(struct args88* a, size_t size) {
  long r = clone3_raw(a, size);
  if (r == 0) _exit(0);
  if (r > 0) {
    waitpid(r, NULL, __WALL);
    return 0;
  }
  return errno;
}

static int child_status(pid_t pid) {
  int st;
  if (waitpid(pid, &st, __WALL) != pid) return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : 1000 + WTERMSIG(st);
}

#if defined(__aarch64__)
// clone3 whose child, in assembly, checks sp == want_sp and (if want_tls)
// tpidr_el0 == want_tls, optionally stores 42 to *flag, then exit_group(77)
// on success or 99 on a mismatch.
static long clone3_asm(void* a, size_t size, uint64_t want_sp, uint64_t want_tls, int* flag) {
  register long x0 __asm__("x0") = (long)a;
  register long x1 __asm__("x1") = (long)size;
  register uint64_t x2 __asm__("x2") = want_sp;
  register uint64_t x3 __asm__("x3") = want_tls;
  register int* x4 __asm__("x4") = flag;
  __asm__ volatile(
      "mov x8, #435\n"
      "svc #0\n"
      "cbnz x0, 3f\n"
      "mov x5, sp\n"
      "cmp x5, x2\n"
      "b.ne 2f\n"
      "cbz x3, 1f\n"
      "mrs x5, tpidr_el0\n"
      "cmp x5, x3\n"
      "b.ne 2f\n"
      "1:\n"
      "cbz x4, 4f\n"
      "mov w5, #42\n"
      "str w5, [x4]\n"
      "4:\n"
      "mov x0, #77\n"
      "mov x8, #94\n"
      "svc #0\n"
      "2:\n"
      "mov x0, #99\n"
      "mov x8, #94\n"
      "svc #0\n"
      "3:\n"
      : "+r"(x0)
      : "r"(x1), "r"(x2), "r"(x3), "r"(x4)
      : "x5", "x8", "memory", "cc");
  if (x0 < 0) {
    errno = -x0;
    return -1;
  }
  return x0;
}
#endif

static void test_caps(void) {
  FILE* f = fopen("/proc/sys/kernel/cap_last_cap", "r");
  int last = -1;
  if (f) {
    if (fscanf(f, "%d", &last) != 1) last = -1;
    fclose(f);
  }
  check(last == 40, "cap_last_cap", "%d (want 40)", last);
  const char* names[] = {"CAP_PERFMON", "CAP_BPF", "CAP_CHECKPOINT_RESTORE"};
  for (int c = 38; c <= 40; c++) {
    errno = 0;
    int r = prctl(PR_CAPBSET_READ, c, 0, 0, 0);
    check(r == 0 || r == 1, names[c - 38], "PR_CAPBSET_READ(%d) = %d errno %d", c, r, errno);
  }
  errno = 0;
  int r = prctl(PR_CAPBSET_READ, 41, 0, 0, 0);
  check(r == -1 && errno == EINVAL, "cap 41 unknown", "PR_CAPBSET_READ(41) = %d errno %d", r, errno);
}

static void test_clone3(void) {
  struct args88 a;
  int e;

  memset(&a, 0, sizeof(a));
  a.exit_signal = SIGCHLD;
  long pid = clone3_raw(&a, 64);
  if (pid == 0) _exit(42);
  check(pid > 0 && child_status(pid) == 42, "fork-like, size 64", "pid %ld errno %d", pid,
        pid < 0 ? errno : 0);

  memset(&a, 0, sizeof(a));
  a.exit_signal = SIGCHLD;
  pid = clone3_raw(&a, sizeof(a));
  if (pid == 0) _exit(43);
  check(pid > 0 && child_status(pid) == 43, "size 88, zero tail", "pid %ld errno %d", pid,
        pid < 0 ? errno : 0);

  memset(&a, 0, sizeof(a));
  a.exit_signal = SIGCHLD;
  a.set_tid_size = 1;
  e = clone3_err(&a, sizeof(a));
  check(e == E2BIG, "size 88, nonzero tail", "errno %d (want E2BIG %d)", e, E2BIG);

  memset(&a, 0, sizeof(a));
  e = clone3_err(&a, 63);
  check(e == EINVAL, "size 63", "errno %d (want EINVAL)", e);
  e = clone3_err(&a, 0);
  check(e == EINVAL, "size 0", "errno %d (want EINVAL)", e);

  static char big[8192];
  e = clone3_err((struct args88*)big, sizeof(big));
  check(e == E2BIG, "size > page", "errno %d (want E2BIG)", e);

  e = clone3_err((struct args88*)8, 64);
  check(e == EFAULT, "bad pointer", "errno %d (want EFAULT)", e);

  memset(&a, 0, sizeof(a));
  a.flags = CLONE_THREAD;
  e = clone3_err(&a, 64);
  check(e == EINVAL, "CLONE_THREAD without SIGHAND", "errno %d (want EINVAL)", e);

  memset(&a, 0, sizeof(a));
  a.flags = SIGCHLD;  // CSIGNAL bits belong in exit_signal
  e = clone3_err(&a, 64);
  check(e == EINVAL, "CSIGNAL in flags", "errno %d (want EINVAL)", e);

  memset(&a, 0, sizeof(a));
  a.flags = 1ULL << 32;
  a.exit_signal = SIGCHLD;
  e = clone3_err(&a, 64);
  check(e == EINVAL, "flag bit 32", "errno %d (want EINVAL)", e);

  memset(&a, 0, sizeof(a));
  a.exit_signal = 0x100 | SIGCHLD;
  e = clone3_err(&a, 64);
  check(e == EINVAL, "exit_signal 0x111", "errno %d (want EINVAL)", e);
  a.exit_signal = 65;
  e = clone3_err(&a, 64);
  check(e == EINVAL, "exit_signal 65", "errno %d (want EINVAL)", e);

  memset(&a, 0, sizeof(a));
  a.flags = CLONE_PARENT;
  a.exit_signal = SIGCHLD;
  e = clone3_err(&a, 64);
  check(e == EINVAL, "CLONE_PARENT with exit_signal", "errno %d (want EINVAL)", e);

  static char stk[16384] __attribute__((aligned(16)));
  memset(&a, 0, sizeof(a));
  a.exit_signal = SIGCHLD;
  a.stack = (uintptr_t)stk;
  e = clone3_err(&a, 64);
  check(e == EINVAL, "stack without size", "errno %d (want EINVAL)", e);
  a.stack = 0;
  a.stack_size = sizeof(stk);
  e = clone3_err(&a, 64);
  check(e == EINVAL, "size without stack", "errno %d (want EINVAL)", e);

  // CLONE_PIDFD and CLONE_PARENT_SETTID together, which legacy clone cannot do.
  int pidfd = -1, ptid = 0;
  memset(&a, 0, sizeof(a));
  a.flags = CLONE_PIDFD | CLONE_PARENT_SETTID;
  a.pidfd = (uintptr_t)&pidfd;
  a.parent_tid = (uintptr_t)&ptid;
  a.exit_signal = SIGCHLD;
  pid = clone3_raw(&a, 64);
  if (pid == 0) _exit(5);
  if (pid > 0) {
    struct pollfd p = {.fd = pidfd, .events = POLLIN};
    int pr = poll(&p, 1, 5000);
    int st = child_status(pid);
    check(pidfd >= 0 && ptid == pid && pr == 1 && st == 5, "CLONE_PIDFD + PARENT_SETTID",
          "pid %ld pidfd %d parent_tid %d poll %d status %d", pid, pidfd, ptid, pr, st);
    close(pidfd);
  } else {
    check(0, "CLONE_PIDFD + PARENT_SETTID", "errno %d", errno);
  }

  // Legacy clone keeps refusing that combination.
  long r = syscall(__NR_clone, CLONE_PIDFD | CLONE_PARENT_SETTID | SIGCHLD, 0, &pidfd, 0, 0);
  if (r == 0) _exit(0);
  if (r > 0) waitpid(r, NULL, 0);
  check(r == -1 && errno == EINVAL, "legacy clone PIDFD + PARENT_SETTID", "r %ld errno %d", r,
        r < 0 ? errno : 0);

  // Child exit signal other than SIGCHLD.
  memset(&a, 0, sizeof(a));
  a.exit_signal = 0;
  pid = clone3_raw(&a, 64);
  if (pid == 0) _exit(6);
  check(pid > 0 && child_status(pid) == 6, "exit_signal 0", "pid %ld", pid);

#if defined(__aarch64__)
  uint64_t top = (uintptr_t)stk + sizeof(stk);

  memset(&a, 0, sizeof(a));
  a.exit_signal = SIGCHLD;
  a.stack = (uintptr_t)stk;
  a.stack_size = sizeof(stk);
  pid = clone3_asm(&a, 64, top, 0, NULL);
  int st = pid > 0 ? child_status(pid) : -1;
  check(st == 77, "child sp = stack + stack_size", "pid %ld status %d", pid, st);

  uint64_t tls = 0x5a5a5a5a1000;
  memset(&a, 0, sizeof(a));
  a.flags = CLONE_SETTLS;
  a.exit_signal = SIGCHLD;
  a.stack = (uintptr_t)stk;
  a.stack_size = sizeof(stk);
  a.tls = tls;
  pid = clone3_asm(&a, 64, top, tls, NULL);
  st = pid > 0 ? child_status(pid) : -1;
  check(st == 77, "CLONE_SETTLS sets tpidr_el0", "pid %ld status %d", pid, st);

  // Legacy clone still takes TLS from its 4th argument (now via copy_thread_tls).
  {
    register long x0 __asm__("x0") = CLONE_SETTLS | SIGCHLD;
    register long x1 __asm__("x1") = (long)top;
    register long x2 __asm__("x2") = 0;
    register long x3 __asm__("x3") = (long)tls;
    register long x4 __asm__("x4") = 0;
    __asm__ volatile(
        "mov x8, #220\n"
        "svc #0\n"
        "cbnz x0, 3f\n"
        "mov x5, sp\n"
        "cmp x5, x1\n"
        "b.ne 2f\n"
        "mrs x5, tpidr_el0\n"
        "cmp x5, x3\n"
        "b.ne 2f\n"
        "mov x0, #77\n"
        "mov x8, #94\n"
        "svc #0\n"
        "2:\n"
        "mov x0, #99\n"
        "mov x8, #94\n"
        "svc #0\n"
        "3:\n"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x4)
        : "x5", "x8", "memory", "cc");
    st = x0 > 0 ? child_status(x0) : -1;
    check(st == 77, "legacy clone CLONE_SETTLS", "pid %ld status %d", (long)x0, st);
  }

  static int flag;
  memset(&a, 0, sizeof(a));
  a.flags = CLONE_VM;
  a.exit_signal = SIGCHLD;
  a.stack = (uintptr_t)stk;
  a.stack_size = sizeof(stk);
  pid = clone3_asm(&a, 64, top, 0, &flag);
  st = pid > 0 ? child_status(pid) : -1;
  check(st == 77 && flag == 42, "CLONE_VM child writes shared memory", "pid %ld status %d flag %d",
        pid, st, flag);
#endif
}

int main(void) {
  printf("clone3test (%s), uid %d\n", sizeof(void*) == 8 ? "arm64" : "arm32 compat", getuid());
  test_caps();
  test_clone3();
  printf("%d failed\n", fails);
  return fails ? 1 : 0;
}
