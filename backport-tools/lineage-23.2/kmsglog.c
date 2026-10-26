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
#define O_TRUNC 01000
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
#define NR_sync 81
#define NR_syncfs 267
#define NR_ftruncate 46
#define NR_setns 268
#define NR_fchdir 50
#define NR_chroot 51
#define NR_reboot 142

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
	struct { s64 s, ns; } ts = { ns / 1000000000, ns % 1000000000 };
	sc(NR_nanosleep, &ts, 0, 0, 0);
}

#define KMSG_CAP (4UL << 20)

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
 * Joins init's mount namespace once second stage is up and appends logcat to
 * /metadata/logcat.txt, restarting it if it dies. The logger's periodic sync()
 * makes the file survive a forced power-off.
 */
static void logcat_saver(void)
{
	sc(NR_mkdirat, AT_FDCWD, "/kl", 0755, 0);
	sc(NR_mkdirat, AT_FDCWD, "/kl/proc", 0755, 0);
	sc6(NR_mount, (s64)"proc", (s64)"/kl/proc", (s64)"proc", 0, 0, 0);
	int procfd = sc(NR_openat, AT_FDCWD, "/kl/proc", O_RDONLY | O_DIRECTORY, 0);
	sayn("logcat: proc fd", procfd);
	if (procfd < 0) return;
	/*
	 * Wait for second stage: init's root then holds system/bin/logcat. setns()
	 * alone leaves us at the namespace's original rootfs, under init's
	 * switch_root, so also chroot to init's root.
	 */
	int root = -1;
	for (int i = 0; i < 60; i++) {
		nap(2000000000L);
		root = sc(NR_openat, procfd, "1/root", O_RDONLY | O_DIRECTORY, 0);
		if (root < 0) continue;
		int f = sc(NR_openat, root, "system/bin/logcat", O_RDONLY, 0);
		if (f >= 0) { sc(NR_close, f, 0, 0, 0); break; }
		sc(NR_close, root, 0, 0, 0);
		root = -1;
	}
	sayn("logcat: init root fd", root);
	if (root < 0) return;
	int ns = sc(NR_openat, procfd, "1/ns/mnt", O_RDONLY, 0);
	sayn("logcat: setns", sc(NR_setns, ns, 0x20000, 0, 0));
	sc(NR_fchdir, root, 0, 0, 0);
	sayn("logcat: chroot", sc(NR_chroot, ".", 0, 0, 0));
	static char *argv[] = { "sh", "-c",
		"while :; do /system/bin/logcat -b all -v threadtime"
		" -f /metadata/logcat.txt -r 1024 -n 2; sleep 2; done", 0 };
	static char *envp[] = { "PATH=/system/bin", 0 };
	sayn("logcat: execve", sc(NR_execve, "/system/bin/sh", argv, envp, 0));
}

/*
 * kmsglog.reboot=<seconds> on the kernel command line: dump blocked tasks, then
 * reboot to the bootloader after that long, so a hung boot test ends without a
 * button press. Only the log's filesystem is synced (syncfs on "out"): sync()
 * would wait on a hung /data and never get to the reboot.
 * kmsglog.sysrq=<seconds> (less than the reboot time) also dumps every task
 * (sysrq t) at that point, for hangs whose cause is not in D state.
 */
static void reboot_timer(int out)
{
	static char cl[4096];
	sc(NR_mkdirat, AT_FDCWD, "/kl", 0755, 0);
	sc(NR_mkdirat, AT_FDCWD, "/kl/p2", 0755, 0);
	sc6(NR_mount, (s64)"proc", (s64)"/kl/p2", (s64)"proc", 0, 0, 0);
	int fd = sc(NR_openat, AT_FDCWD, "/kl/p2/cmdline", O_RDONLY, 0);
	if (fd < 0) return;
	s64 n = sc(NR_read, fd, cl, sizeof(cl) - 1, 0);
	sc(NR_close, fd, 0, 0, 0);
	if (n <= 0) return;
	cl[n] = 0;
	u64 secs = 0, tsecs = 0;
	for (char *p = cl; *p; p++) {
		if (sprefix(p, "kmsglog.reboot=")) secs = satou(p + 15);
		if (sprefix(p, "kmsglog.sysrq=")) tsecs = satou(p + 14);
	}
	if (!secs) return;
	sayn("reboot to bootloader in seconds:", secs);
	int sysrq = sc(NR_openat, AT_FDCWD, "/kl/p2/sysrq-trigger", O_WRONLY, 0);
	if (tsecs && tsecs < secs) {
		sayn("task dump in seconds:", tsecs);
		nap(tsecs * 1000000000L);
		sc(NR_write, sysrq, "t", 1, 0);	/* every task, any state */
		secs -= tsecs;
	}
	nap(secs * 1000000000L);
	sc(NR_write, sysrq, "w", 1, 0);	/* blocked (D state) tasks */
	sc(NR_write, sysrq, "l", 1, 0);	/* backtraces of all CPUs */
	nap(3000000000L);	/* let the logger write them out */
	say("rebooting to bootloader");
	nap(500000000L);
	sc(NR_syncfs, out, 0, 0, 0);
	sc(NR_reboot, 0xfee1dead, 672274793, 0xa1b2c3d4, "bootloader");
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
				 O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_SYNC, 0644);
		if (i == 0 || out >= 0) sayn("mount metadata", r);
		if (out < 0) nap(50000000);
	}
	sayn("log file fd", out);
	if (out < 0) return;
	static const char hdr[] = "\n===== kmsglog: new boot =====\n";
	sc(NR_write, out, hdr, sizeof(hdr) - 1, 0);
	if (sc6(NR_clone, 17, 0, 0, 0, 0, 0) == 0) {	/* SIGCHLD */
		logcat_saver();
		sc(NR_exit, 0, 0, 0, 0);
	}
	if (sc6(NR_clone, 17, 0, 0, 0, 0, 0) == 0) {
		reboot_timer(out);
		sc(NR_exit, 0, 0, 0, 0);
	}
	/* Drain everything available, then one synced write; nap when idle. */
	static char big[1 << 20];
	u64 written = 0;
	for (u64 iter = 0;; iter++) {
		if (iter % 50 == 0) sc(NR_syncfs, out, 0, 0, 0);	/* flush logcat.txt */
		u64 len = 0;
		for (;;) {
			if (len + 8192 > sizeof(big)) break;
			s64 n = sc(NR_read, in, big + len, 8192, 0);
			if (n == -32) continue;		/* EPIPE: records overwritten */
			if (n <= 0) break;		/* -EAGAIN: nothing more now */
			len += n;
		}
		/*
		 * /metadata is 10 MB and also holds vold keys and aconfig flags, so
		 * keep at most the newest KMSG_CAP bytes: start the file over when full.
		 */
		if (len && written + len > KMSG_CAP) {
			sc(NR_ftruncate, out, 0, 0, 0);
			sc(NR_write, out, "\n===== kmsglog: wrapped =====\n", 30, 0);
			written = 0;
		}
		if (len) { sc(NR_write, out, big, len, 0); written += len; }
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
