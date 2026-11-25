// userfaultfd and mremap(MREMAP_DONTUNMAP) as ART's concurrent mark-compact GC uses them:
// UFFD_USER_MODE_ONLY, UFFD_FEATURE_SIGBUS, UFFDIO_COPY/ZEROPAGE, and moving a registered heap
// with MREMAP_DONTUNMAP (the old range stays registered, the new one is not).
// Run as root: some checks drop to an app uid. Exit 0 when every check passes.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MREMAP_DONTUNMAP
#define MREMAP_DONTUNMAP 4
#endif
#ifndef UFFD_USER_MODE_ONLY
#define UFFD_USER_MODE_ONLY 1
#endif
#ifndef UFFD_FEATURE_SIGBUS
#define UFFD_FEATURE_SIGBUS (1 << 7)
#endif

static int passed, failed;
static long pg;

static void check(int ok, const char *what) {
  if (ok) passed++; else { failed++; printf("FAIL %s (errno %d %s)\n", what, errno, strerror(errno)); }
}

static void *map(size_t n, int flags) {
  void *p = mmap(NULL, n * pg, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); exit(2); }
  return p;
}

static int uffd_open(int flags) { return syscall(__NR_userfaultfd, flags); }

static int uffd_api(int fd, uint64_t want, uint64_t *got) {
  struct uffdio_api a = { .api = UFFD_API, .features = want };
  int r = ioctl(fd, UFFDIO_API, &a);
  if (got) *got = a.features;
  return r;
}

static int uffd_register(int fd, void *p, size_t n) {
  struct uffdio_register r = { .range = { (uintptr_t)p, n * pg }, .mode = UFFDIO_REGISTER_MODE_MISSING };
  return ioctl(fd, UFFDIO_REGISTER, &r);
}

static sigjmp_buf jb;
static volatile sig_atomic_t sigbus_seen;
static void on_sigbus(int s) { (void)s; sigbus_seen = 1; siglongjmp(jb, 1); }

// 1 if reading *p raised SIGBUS, 0 if it read a value (stored in *v)
static int read_faults(volatile char *p, char *v) {
  sigbus_seen = 0;
  if (sigsetjmp(jb, 1) == 0) { *v = *p; return 0; }
  return 1;
}

static long vmlck_kb(void) {
  FILE *f = fopen("/proc/self/status", "r"); char l[256]; long kb = -1;
  while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "VmLck:", 6)) kb = atol(l + 6);
  if (f) fclose(f);
  return kb;
}

static void test_mremap(void) {
  // private anonymous: pages move, the old range stays mapped and empty
  char *a = map(4, MAP_PRIVATE | MAP_ANONYMOUS);
  memset(a, 'x', 4 * pg);
  char *b = mremap(a, 4 * pg, 4 * pg, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, NULL);
  check(b != MAP_FAILED, "mremap DONTUNMAP private anon");
  if (b != MAP_FAILED) {
    check(b[0] == 'x' && b[4 * pg - 1] == 'x', "moved pages keep their data");
    check(a[0] == 0 && a[3 * pg] == 0, "old range stays mapped and reads zero");
    a[0] = 'y';
    check(b[0] == 'x', "old and new ranges are separate memory");
    munmap(b, 4 * pg);
  }
  munmap(a, 4 * pg);

  // the probe ART runs: one page of shared anonymous memory
  char *s = map(1, MAP_SHARED | MAP_ANONYMOUS);
  s[0] = 's';
  char *t = mremap(s, pg, pg, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, NULL);
  check(t != MAP_FAILED, "mremap DONTUNMAP shared anon (ART's probe)");
  if (t != MAP_FAILED) {
    check(t[0] == 's' && s[0] == 's', "shared: both ranges map the same object");
    munmap(t, pg);
  }
  munmap(s, pg);

  // argument checks
  char *c = map(2, MAP_PRIVATE | MAP_ANONYMOUS);
  errno = 0;
  check(mremap(c, pg, pg, MREMAP_DONTUNMAP, NULL) == MAP_FAILED && errno == EINVAL,
        "DONTUNMAP without MAYMOVE -> EINVAL");
  errno = 0;
  check(mremap(c, pg, 2 * pg, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, NULL) == MAP_FAILED && errno == EINVAL,
        "DONTUNMAP with a new size -> EINVAL");
  errno = 0;
  check(mremap(c, pg, pg, 8, NULL) == MAP_FAILED && errno == EINVAL, "unknown mremap flag -> EINVAL");

  // with MREMAP_FIXED to a chosen address
  char *d = map(2, MAP_PRIVATE | MAP_ANONYMOUS);
  c[0] = 'f';
  char *e = mremap(c, pg, pg, MREMAP_MAYMOVE | MREMAP_FIXED | MREMAP_DONTUNMAP, d + pg);
  check(e == d + pg, "DONTUNMAP with FIXED lands at the address");
  if (e == d + pg) check(e[0] == 'f' && c[0] == 0, "FIXED: data moved, old range empty");
  munmap(c, 2 * pg); munmap(d, 2 * pg);

  // locked memory: moving part of a locked mapping keeps mm->locked_vm right
  long before = vmlck_kb();
  char *l = map(4, MAP_PRIVATE | MAP_ANONYMOUS);
  if (mlock(l, 4 * pg) == 0) {
    long locked = vmlck_kb();
    char *m = mremap(l + pg, 2 * pg, 2 * pg, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, NULL);
    check(m != MAP_FAILED, "DONTUNMAP of the middle of a locked mapping");
    check(vmlck_kb() == locked, "VmLck unchanged by the move");
    if (m != MAP_FAILED) munmap(m, 2 * pg);
    munmap(l, 4 * pg);
    check(vmlck_kb() == before, "VmLck back to its start value after unmapping both");
  } else {
    printf("skip locked-memory checks: mlock %s\n", strerror(errno));
    munmap(l, 4 * pg);
  }
}

struct handler { int fd; char fill; int served; };
static void *serve(void *arg) {
  struct handler *h = arg;
  struct uffd_msg msg;
  char *page = map(1, MAP_PRIVATE | MAP_ANONYMOUS);
  memset(page, h->fill, pg);
  while (h->served < 2 && read(h->fd, &msg, sizeof msg) == sizeof msg) {
    if (msg.event != UFFD_EVENT_PAGEFAULT) continue;
    struct uffdio_copy c = { .dst = msg.arg.pagefault.address & ~(pg - 1), .src = (uintptr_t)page, .len = pg };
    if (ioctl(h->fd, UFFDIO_COPY, &c) == 0) h->served++;
  }
  return NULL;
}

static void test_uffd(void) {
  uint64_t feat = 0;
  int fd;

  errno = 0;
  check(uffd_open(O_CLOEXEC | 2) == -1 && errno == EINVAL, "unknown userfaultfd flag -> EINVAL");

  fd = uffd_open(O_CLOEXEC | UFFD_USER_MODE_ONLY);
  check(fd >= 0, "userfaultfd(UFFD_USER_MODE_ONLY)");
  if (fd < 0) return;
  check(uffd_api(fd, 0, &feat) == 0, "UFFDIO_API with no features");
  check(feat & UFFD_FEATURE_SIGBUS, "UFFDIO_API reports UFFD_FEATURE_SIGBUS");
  close(fd);
  fd = uffd_open(O_CLOEXEC | UFFD_USER_MODE_ONLY);
  errno = 0;
  check(uffd_api(fd, 1ull << 40, NULL) == -1 && errno == EINVAL, "UFFDIO_API with an unknown feature -> EINVAL");
  close(fd);

  // ART's MMAP_TRYLOCK probe: an unknown ZEROPAGE mode fails with EINVAL and zeropage untouched
  fd = uffd_open(O_CLOEXEC | UFFD_USER_MODE_ONLY);
  uffd_api(fd, 0, NULL);
  char *z = map(1, MAP_PRIVATE | MAP_ANONYMOUS);
  struct uffdio_zeropage zp = { .range = { (uintptr_t)z, pg }, .mode = 1ull << 1, .zeropage = 0 };
  errno = 0;
  check(ioctl(fd, UFFDIO_ZEROPAGE, &zp) == -1 && errno == EINVAL && zp.zeropage == 0,
        "unknown ZEROPAGE mode -> EINVAL, zeropage untouched");
  munmap(z, pg); close(fd);

  // SIGBUS mode: a missing page raises SIGBUS until UFFDIO_COPY or UFFDIO_ZEROPAGE fills it
  struct sigaction sa = { .sa_handler = on_sigbus }, old;
  sigaction(SIGBUS, &sa, &old);
  fd = uffd_open(O_CLOEXEC | UFFD_USER_MODE_ONLY);
  check(uffd_api(fd, UFFD_FEATURE_SIGBUS, NULL) == 0, "UFFDIO_API enabling SIGBUS");
  char *h = map(4, MAP_PRIVATE | MAP_ANONYMOUS);
  check(uffd_register(fd, h, 4) == 0, "UFFDIO_REGISTER missing mode");
  char v = 0;
  check(read_faults(h, &v) == 1, "SIGBUS mode: missing page raises SIGBUS");
  char *src = map(1, MAP_PRIVATE | MAP_ANONYMOUS);
  memset(src, 'c', pg);
  struct uffdio_copy cp = { .dst = (uintptr_t)h, .src = (uintptr_t)src, .len = pg };
  check(ioctl(fd, UFFDIO_COPY, &cp) == 0 && cp.copy == pg, "UFFDIO_COPY");
  check(read_faults(h, &v) == 0 && v == 'c', "copied page reads back");
  struct uffdio_zeropage zz = { .range = { (uintptr_t)h + pg, pg } };
  check(ioctl(fd, UFFDIO_ZEROPAGE, &zz) == 0 && zz.zeropage == pg, "UFFDIO_ZEROPAGE");
  check(read_faults(h + pg, &v) == 0 && v == 0, "zero page reads zero");
  // the errors ART tolerates: EEXIST for a page already there, ENOENT once the range is gone
  errno = 0;
  check(ioctl(fd, UFFDIO_ZEROPAGE, &zz) == -1 && errno == EEXIST, "ZEROPAGE on a present page -> EEXIST");
  errno = 0;
  check(ioctl(fd, UFFDIO_COPY, &cp) == -1 && errno == EEXIST, "COPY on a present page -> EEXIST");
  char *un = map(1, MAP_PRIVATE | MAP_ANONYMOUS);
  struct uffdio_zeropage zu = { .range = { (uintptr_t)un, pg } };
  errno = 0;
  check(ioctl(fd, UFFDIO_ZEROPAGE, &zu) == -1 && errno == ENOENT, "ZEROPAGE on an unregistered range -> ENOENT");
  munmap(un, pg);
  errno = 0;
  check(ioctl(fd, UFFDIO_ZEROPAGE, &zu) == -1 && errno == ENOENT, "ZEROPAGE on an unmapped range -> ENOENT");
  // a kernel-mode access to a missing page fails with EFAULT instead of blocking
  int pfd[2]; pipe(pfd); write(pfd[1], "k", 1);
  errno = 0;
  check(read(pfd[0], h + 2 * pg, 1) == -1 && errno == EFAULT, "kernel access to a missing page -> EFAULT");
  close(pfd[0]); close(pfd[1]);

  // ART's compaction: move the registered heap away with MREMAP_DONTUNMAP
  h[pg] = 'm';  // page 1 holds data; pages 2 and 3 are missing
  char *from = mremap(h, 4 * pg, 4 * pg, MREMAP_MAYMOVE | MREMAP_DONTUNMAP, NULL);
  check(from != MAP_FAILED, "mremap DONTUNMAP of a registered range");
  if (from != MAP_FAILED) {
    check(read_faults(from + pg, &v) == 0 && v == 'm', "new range has the data");
    check(read_faults(from + 3 * pg, &v) == 0 && v == 0, "new range is not registered: a hole reads zero");
    check(read_faults(h + pg, &v) == 1, "old range is still registered: SIGBUS");
    cp.dst = (uintptr_t)h + pg;
    check(ioctl(fd, UFFDIO_COPY, &cp) == 0, "UFFDIO_COPY into the old range");
    check(read_faults(h + pg, &v) == 0 && v == 'c', "old range reads the copied page");
    munmap(from, 4 * pg);
  }
  munmap(h, 4 * pg); munmap(src, pg); close(fd);
  sigaction(SIGBUS, &old, NULL);

  // without SIGBUS: user-mode faults are events served by another thread
  fd = uffd_open(O_CLOEXEC | UFFD_USER_MODE_ONLY);
  uffd_api(fd, 0, NULL);
  char *e = map(2, MAP_PRIVATE | MAP_ANONYMOUS);
  uffd_register(fd, e, 2);
  struct handler hd = { .fd = fd, .fill = 'e' };
  pthread_t th; pthread_create(&th, NULL, serve, &hd);
  int ok = e[0] == 'e' && e[pg] == 'e';
  pthread_join(th, NULL);  // the handler stops after two faults
  check(ok && hd.served == 2, "event mode: two faults served by UFFDIO_COPY");
  close(fd);
  munmap(e, 2 * pg);
}

// As an app uid without CAP_SYS_PTRACE, with vm.unprivileged_userfaultfd 0.
static void test_unprivileged(void) {
  char s[8] = "?"; int f = open("/proc/sys/vm/unprivileged_userfaultfd", O_RDONLY);
  if (f >= 0) { read(f, s, sizeof s - 1); close(f); }
  if (s[0] != '0') { printf("skip unprivileged checks: vm.unprivileged_userfaultfd is %c\n", s[0]); return; }
  pid_t p = fork();
  if (p == 0) {
    gid_t g = 10199;
    if (setgroups(1, &g) || setresgid(10199, 10199, 10199) || setresuid(10199, 10199, 10199)) _exit(3);
    int a = uffd_open(O_CLOEXEC | UFFD_USER_MODE_ONLY);
    errno = 0;
    int b = uffd_open(O_CLOEXEC);
    _exit((a >= 0 ? 0 : 1) | (b == -1 && errno == EPERM ? 0 : 2));
  }
  int st = 0; waitpid(p, &st, 0);
  int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 99;
  if (rc == 3) { printf("skip unprivileged checks: setuid failed\n"); return; }
  check(!(rc & 1) && rc != 99, "app uid: userfaultfd(UFFD_USER_MODE_ONLY) allowed");
  check(!(rc & 2) && rc != 99, "app uid: userfaultfd without it -> EPERM");
}

int main(void) {
  pg = sysconf(_SC_PAGESIZE);
  setvbuf(stdout, NULL, _IOLBF, 0);
  test_mremap();
  test_uffd();
  test_unprivileged();
  printf("uffdtest: %d passed, %d failed\n", passed, failed);
  return failed != 0;
}
