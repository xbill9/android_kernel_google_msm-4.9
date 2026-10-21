/*
 * kmsglog: stand-in /init for boot tests. Mounts the metadata partition,
 * forks a child that appends /dev/kmsg to /metadata/boot-kmsg.txt with O_SYNC
 * writes, and execs the real first-stage init (/init.real) as PID 1.
 * Freestanding: raw aarch64 syscalls, no libc.
 */
typedef unsigned long u64;
typedef long s64;

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_APPEND 02000
#define O_SYNC 04010000
#define O_NONBLOCK 04000
#define O_DIRECTORY 040000	/* arm64 value */
#define S_IFBLK 0060000
#define S_IFCHR 0020000

static s64 sc6(s64 n, s64 a, s64 b, s64 c, s64 d, s64 e, s64 f)
{
	register s64 x8 asm("x8") = n;
	register s64 x0 asm("x0") = a;
	register s64 x1 asm("x1") = b;
	register s64 x2 asm("x2") = c;
	register s64 x3 asm("x3") = d;
	register s64 x4 asm("x4") = e;
	register s64 x5 asm("x5") = f;
	asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3),
		     "r"(x4), "r"(x5) : "memory");
	return x0;
}
#define sc(n, a, b, c, d) sc6(n, (s64)(a), (s64)(b), (s64)(c), (s64)(d), 0, 0)

#define NR_mkdirat 34
#define NR_mknodat 33
#define NR_mount 40
#define NR_openat 56
#define NR_close 57
#define NR_getdents64 61
#define NR_read 63
#define NR_write 64
#define NR_exit 93
#define NR_nanosleep 101
#define NR_clone 220
#define NR_execve 221

static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static int sprefix(const char *s, const char *p)
{
	while (*p) if (*s++ != *p++) return 0;
	return 1;
}
static void scopy(char *d, const char *s) { while ((*d++ = *s++)); }
static void scat(char *d, const char *s) { scopy(d + slen(d), s); }
static u64 satou(const char *s)
{
	u64 n = 0;
	while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
	return n;
}

#define NR_newfstatat 79

static void nap(s64 ns)
{
	struct { s64 s, ns; } ts = { 0, ns };
	sc(NR_nanosleep, &ts, 0, 0, 0);
}

static int kw = -1;	/* /dev/kmsg for status lines */
static void say(const char *m)
{
	static char b[128];
	scopy(b, "kmsglog: "); scat(b, m); scat(b, "\n");
	if (kw >= 0) sc(NR_write, kw, b, slen(b), 0);
}
static void sayn(const char *m, s64 v)
{
	static char b[96], d[24];
	int i = 23, neg = v < 0;
	u64 u = neg ? -v : v;
	d[i] = 0;
	do { d[--i] = '0' + u % 10; u /= 10; } while (u);
	if (neg) d[--i] = '-';
	scopy(b, m); scat(b, " "); scat(b, d + i);
	say(b);
}

/* MAJOR/MINOR of the block device whose PARTNAME is metadata, from sysfs at /kl/sys. */
static int find_metadata(int sysfd, u64 *maj, u64 *min)
{
	static char buf[4096], path[256], ue[1024];
	int dfd = sc(NR_openat, sysfd, "class/block", O_RDONLY | O_DIRECTORY, 0);
	if (dfd < 0) return -1;
	for (;;) {
		s64 n = sc(NR_getdents64, dfd, buf, sizeof(buf), 0);
		if (n <= 0) break;
		for (s64 off = 0; off < n;) {
			unsigned short reclen = *(unsigned short *)(buf + off + 16);
			char *name = buf + off + 19;
			off += reclen;
			if (name[0] == '.') continue;
			scopy(path, "class/block/");
			scat(path, name);
			scat(path, "/uevent");
			int fd = sc(NR_openat, sysfd, path, O_RDONLY, 0);
			if (fd < 0) continue;
			s64 r = sc(NR_read, fd, ue, sizeof(ue) - 1, 0);
			sc(NR_close, fd, 0, 0, 0);
			if (r <= 0) continue;
			ue[r] = 0;
			u64 ma = 0, mi = 0; int hit = 0;
			for (char *l = ue; *l;) {
				if (sprefix(l, "MAJOR=")) ma = satou(l + 6);
				if (sprefix(l, "MINOR=")) mi = satou(l + 6);
				if (sprefix(l, "PARTNAME=metadata\n")) hit = 1;
				while (*l && *l != '\n') l++;
				if (*l) l++;
			}
			if (hit) { *maj = ma; *min = mi; sc(NR_close, dfd, 0, 0, 0); return 0; }
		}
	}
	sc(NR_close, dfd, 0, 0, 0);
	return -1;
}

/*
 * Runs in its own mount namespace (see _start), so the sysfs and metadata
 * mounts here are invisible to init.
 */
static void logger(void)
{
	u64 maj, min;
	int i;

	sc(NR_mknodat, AT_FDCWD, "/kmsglog.kmsg", S_IFCHR | 0600, (1 << 8) | 11);
	int in = sc(NR_openat, AT_FDCWD, "/kmsglog.kmsg", O_RDONLY | O_NONBLOCK, 0);
	kw = sc(NR_openat, AT_FDCWD, "/kmsglog.kmsg", O_WRONLY, 0);
	sayn("started, kmsg fd", in);
	if (in < 0) return;

	/*
	 * init deletes everything on the ramdisk once it switches root, and
	 * removing a mount point detaches the mount even in this namespace.
	 * So hold fds, not paths, and retry if the cleanup wins a race.
	 */
	int sysfd = -1, out = -1;
	for (i = 0; sysfd < 0 && i < 50; i++) {
		sc(NR_mkdirat, AT_FDCWD, "/kl", 0755, 0);
		sc(NR_mkdirat, AT_FDCWD, "/kl/sys", 0755, 0);
		if (sc6(NR_mount, (s64)"sysfs", (s64)"/kl/sys", (s64)"sysfs", 0, 0, 0) == 0)
			sysfd = sc(NR_openat, AT_FDCWD, "/kl/sys", O_RDONLY | O_DIRECTORY, 0);
		if (sysfd < 0) nap(10000000);
	}
	sayn("sysfs fd", sysfd);
	if (sysfd < 0) return;
	for (i = 0; find_metadata(sysfd, &maj, &min) < 0; i++) {
		if (i > 600) { say("metadata partition not found"); return; }
		nap(50000000);
	}
	sayn("metadata minor", min);
	u64 dev = ((maj & 0xfff) << 8) | (min & 0xff) | ((min & ~0xffUL) << 12);
	for (i = 0; out < 0 && i < 100; i++) {
		sc(NR_mkdirat, AT_FDCWD, "/kl", 0755, 0);
		sc(NR_mkdirat, AT_FDCWD, "/kl/mnt", 0755, 0);
		sc(NR_mknodat, AT_FDCWD, "/kl/md", S_IFBLK | 0600, dev);
		s64 r = sc6(NR_mount, (s64)"/kl/md", (s64)"/kl/mnt", (s64)"ext4", 0, 0, 0);
		if (r == 0)
			out = sc(NR_openat, AT_FDCWD, "/kl/mnt/boot-kmsg.txt",
				 O_WRONLY | O_CREAT | O_APPEND | O_SYNC, 0644);
		if (i == 0 || out >= 0) sayn("mount metadata", r);
		if (out < 0) nap(50000000);
	}
	sayn("log file fd", out);
	if (out < 0) return;
	static const char hdr[] = "\n===== kmsglog: new boot =====\n";
	sc(NR_write, out, hdr, sizeof(hdr) - 1, 0);
	/* Drain everything available, then one synced write; nap when idle. */
	static char big[1 << 20];
	for (;;) {
		u64 len = 0;
		for (;;) {
			if (len + 8192 > sizeof(big)) break;
			s64 n = sc(NR_read, in, big + len, 8192, 0);
			if (n == -32) continue;		/* EPIPE: records overwritten */
			if (n <= 0) break;		/* -EAGAIN: nothing more now */
			len += n;
		}
		if (len) sc(NR_write, out, big, len, 0);
		else nap(20000000);
	}
}

void _start(void)
{
	static char *argv[] = { "/init", 0 };
	static char *envp[] = { 0 };
	/* fork into a private mount namespace: CLONE_NEWNS | SIGCHLD */
	s64 pid = sc6(NR_clone, 0x20000 | 17, 0, 0, 0, 0, 0);
	if (pid == 0) {
		logger();
		sc(NR_exit, 0, 0, 0, 0);
	}
	sc(NR_execve, "/init.real", argv, envp, 0);
	sc(NR_exit, 1, 0, 0, 0);
}
