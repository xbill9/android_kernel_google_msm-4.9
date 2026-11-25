// MADV_COLD, MADV_PAGEOUT and process_madvise(2) as Android's CachedAppOptimizer uses them to
// compact cached apps: checks the effect (VmSwap/VmRSS, active page flags), not only return codes.
// Run as root (pagemap pfns, kpageflags, CAP_SYS_NICE). Needs swap (zram). Exit 0 when all pass.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MADV_COLD
#define MADV_COLD 20
#endif
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif
#ifndef __NR_process_madvise
#define __NR_process_madvise 440
#endif
#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#endif
#define KPF_ACTIVE 6
#define MB (1024 * 1024)

static int passed, failed;
static long pg;
static void check(int ok, const char *what) {
  if (ok) passed++; else { failed++; printf("FAIL %s (errno %d %s)\n", what, errno, strerror(errno)); }
}
static long pmadvise(int fd, struct iovec *v, size_t n, int adv, unsigned f) {
  return syscall(__NR_process_madvise, fd, v, n, adv, f);
}
static int pidfd_open(pid_t p) { return syscall(__NR_pidfd_open, p, 0); }

static long status_kb(pid_t pid, const char *key) {  // VmRSS, VmSwap ...
  char path[64], l[256]; long kb = -1; size_t n = strlen(key);
  snprintf(path, sizeof path, "/proc/%d/status", pid);
  FILE *f = fopen(path, "r");
  while (f && fgets(l, sizeof l, f)) if (!strncmp(l, key, n) && l[n] == ':') kb = atol(l + n + 1);
  if (f) fclose(f);
  return kb;
}

// fraction (0..100) of the present pages of [p, p+len) whose struct page is on an active list
static int active_pct(char *p, size_t len) {
  int pm = open("/proc/self/pagemap", O_RDONLY), kf = open("/proc/kpageflags", O_RDONLY);
  int present = 0, active = 0;
  for (size_t off = 0; off < len; off += pg) {
    uint64_t e = 0, flags = 0;
    if (pread(pm, &e, 8, ((uintptr_t)p + off) / pg * 8) != 8 || !(e >> 63)) continue;
    uint64_t pfn = e & ((1ull << 55) - 1);
    if (!pfn || pread(kf, &flags, 8, pfn * 8) != 8) continue;
    present++;
    if (flags >> KPF_ACTIVE & 1) active++;
  }
  close(pm); close(kf);
  return present ? active * 100 / present : -1;
}

static char *touched(size_t len, char fill) {
  char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); exit(2); }
  memset(p, fill, len);
  return p;
}

static int intact(char *p, size_t len, char fill) {
  for (size_t i = 0; i < len; i += 512) if (p[i] != fill) return 0;
  return 1;
}

static void test_self(void) {
  size_t len = 64 * MB;
  char *a = touched(len, 'a');
  // touch twice so the pages are surely on the active list
  memset(a, 'a', len);
  int before = active_pct(a, len);
  check(madvise(a, len, MADV_COLD) == 0, "madvise MADV_COLD");
  int after = active_pct(a, len);
  printf("MADV_COLD: active pages %d%% -> %d%%\n", before, after);
  check(before >= 50 && after <= 10, "MADV_COLD moves the pages to the inactive list");
  check(intact(a, len, 'a'), "data intact after MADV_COLD");
  munmap(a, len);

  char *b = touched(len, 'b');
  long rss0 = status_kb(getpid(), "VmRSS"), swap0 = status_kb(getpid(), "VmSwap");
  check(madvise(b, len, MADV_PAGEOUT) == 0, "madvise MADV_PAGEOUT");
  long rss1 = status_kb(getpid(), "VmRSS"), swap1 = status_kb(getpid(), "VmSwap");
  printf("MADV_PAGEOUT of 65536 kB: VmRSS %ld -> %ld kB, VmSwap %ld -> %ld kB\n", rss0, rss1, swap0, swap1);
  check(swap1 - swap0 >= 60000, "MADV_PAGEOUT puts the range in swap");
  check(rss0 - rss1 >= 60000, "MADV_PAGEOUT drops it from RSS");
  check(intact(b, len, 'b'), "data intact after swap-in");
  munmap(b, len);

  // pages shared with a child (CoW after fork) are left alone
  char *c = touched(16 * MB, 'c');
  pid_t k = fork();
  if (k == 0) { pause(); _exit(0); }
  long s0 = status_kb(getpid(), "VmSwap");
  madvise(c, 16 * MB, MADV_PAGEOUT);
  long s1 = status_kb(getpid(), "VmSwap");
  check(s1 - s0 < 1024, "MADV_PAGEOUT leaves pages shared with another process");
  kill(k, SIGKILL); waitpid(k, NULL, 0);
  munmap(c, 16 * MB);

  char *l = touched(pg, 'l');
  if (mlock(l, pg) == 0) {
    errno = 0;
    check(madvise(l, pg, MADV_PAGEOUT) == -1 && errno == EINVAL, "MADV_PAGEOUT on locked memory -> EINVAL");
    errno = 0;
    check(madvise(l, pg, MADV_COLD) == -1 && errno == EINVAL, "MADV_COLD on locked memory -> EINVAL");
  }
  munmap(l, pg);
}

// child: two 32 MiB ranges touched; sends their addresses, then waits
static pid_t spawn(char **r1, char **r2) {
  int p[2]; pipe(p);
  pid_t c = fork();
  if (c == 0) {
    char *x = touched(32 * MB, 'x'), *y = touched(32 * MB, 'y');
    char *a[2] = { x, y };
    write(p[1], a, sizeof a);
    pause();
    _exit(0);
  }
  char *a[2]; read(p[0], a, sizeof a); close(p[0]); close(p[1]);
  *r1 = a[0]; *r2 = a[1];
  return c;
}

static void test_process(void) {
  char *x, *y;
  pid_t c = spawn(&x, &y);
  int fd = pidfd_open(c);
  struct iovec v[2] = { { x, 32 * MB }, { y, 32 * MB } };

  errno = 0;
  check(pmadvise(fd, v, 2, MADV_PAGEOUT, 1) == -1 && errno == EINVAL, "process_madvise flags -> EINVAL");
  errno = 0;
  check(pmadvise(fd, v, 2, MADV_DONTNEED, 0) == -1 && errno == EINVAL, "process_madvise MADV_DONTNEED -> EINVAL");
  int nf = open("/dev/null", O_RDONLY);
  errno = 0;
  check(pmadvise(nf, v, 2, MADV_COLD, 0) == -1 && errno == EBADF, "process_madvise on a non-pidfd -> EBADF");
  close(nf);

  long swap0 = status_kb(c, "VmSwap");
  long r = pmadvise(fd, v, 2, MADV_PAGEOUT, 0);
  long swap1 = status_kb(c, "VmSwap");
  printf("process_madvise MADV_PAGEOUT: returned %ld, child VmSwap %ld -> %ld kB\n", r, swap0, swap1);
  check(r == 64 * MB, "process_madvise returns the bytes advised");
  check(swap1 - swap0 >= 60000, "process_madvise MADV_PAGEOUT puts the child's memory in swap");
  check(pmadvise(fd, v, 1, MADV_COLD, 0) == 32 * MB, "process_madvise MADV_COLD");

  // a range the child has no mapping for: the bytes before it are still reported
  struct iovec w[2] = { { x, 32 * MB }, { (char *)0x1000, pg } };
  errno = 0;
  check(pmadvise(fd, w, 2, MADV_COLD, 0) == 32 * MB, "partial: bytes before an unmapped range");
  errno = 0;
  check(pmadvise(fd, w + 1, 1, MADV_COLD, 0) == -1 && errno == ENOMEM, "only an unmapped range -> ENOMEM");

  // without CAP_SYS_NICE
  pid_t u = fork();
  if (u == 0) {
    gid_t g = 10199;
    if (setgroups(1, &g) || setresgid(10199, 10199, 10199) || setresuid(10199, 10199, 10199)) _exit(3);
    errno = 0;
    long q = pmadvise(fd, v, 1, MADV_COLD, 0);
    _exit(q == -1 && (errno == EPERM || errno == EACCES) ? 0 : 1);
  }
  int st; waitpid(u, &st, 0);
  check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "app uid without CAP_SYS_NICE or ptrace access -> EPERM or EACCES");

  kill(c, SIGKILL); waitpid(c, NULL, 0);
  errno = 0;
  check(pmadvise(fd, v, 1, MADV_COLD, 0) == -1 && errno == ESRCH, "exited process -> ESRCH");
  close(fd);
}

int main(void) {
  pg = sysconf(_SC_PAGESIZE);
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (access("/proc/kpageflags", R_OK)) { printf("needs root\n"); return 2; }
  test_self();
  test_process();
  printf("madvisetest: %d passed, %d failed\n", passed, failed);
  return failed != 0;
}
