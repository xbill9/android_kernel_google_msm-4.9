/* Attach a BPF kprobe-type program to a uprobe on our own function via
 * tracefs uprobe_events + PERF_EVENT_IOC_SET_BPF, call it N times, and
 * check that the program counted N hits. */
#include <errno.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define T "/sys/kernel/tracing"
#define N 1000

__attribute__((noinline)) int target(int x)
{
	asm volatile("" ::: "memory");
	return x + 1;
}

static long bpf(int cmd, union bpf_attr *a)
{
	return syscall(__NR_bpf, cmd, a, sizeof(*a));
}

static int wr(const char *path, const char *s, int append)
{
	int fd = open(path, O_WRONLY | (append ? O_APPEND : O_TRUNC));
	if (fd < 0 || write(fd, s, strlen(s)) < 0) {
		fprintf(stderr, "write %s: %s\n", path, strerror(errno));
		return -1;
	}
	close(fd);
	return 0;
}

int main(void)
{
	char exe[256], line[512], buf[512];
	unsigned long start, end, pgoff, off = 0;
	unsigned long addr = (unsigned long)target;
	FILE *f = fopen("/proc/self/maps", "r");

	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &pgoff) == 3 &&
		    addr >= start && addr < end) {
			off = addr - start + pgoff;
			break;
		}
	}
	fclose(f);
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	exe[n] = 0;

	snprintf(buf, sizeof(buf), "p:uprobes/bpftest %s:0x%lx\n", exe, off);
	printf("probe: %s", buf);
	wr(T "/uprobe_events", "-:uprobes/bpftest\n", 1);
	if (wr(T "/uprobe_events", buf, 1))
		return 1;

	f = fopen(T "/events/uprobes/bpftest/id", "r");
	int id = -1;
	if (!f || fscanf(f, "%d", &id) != 1) {
		perror("event id");
		return 1;
	}
	fclose(f);

	union bpf_attr a;
	memset(&a, 0, sizeof(a));
	a.map_type = BPF_MAP_TYPE_ARRAY;
	a.key_size = 4;
	a.value_size = 8;
	a.max_entries = 1;
	int map = bpf(BPF_MAP_CREATE, &a);
	if (map < 0) {
		perror("map create");
		return 1;
	}

	struct bpf_insn prog[] = {
		/* *(u32 *)(fp - 4) = 0 */
		{ BPF_ST | BPF_MEM | BPF_W, BPF_REG_10, 0, -4, 0 },
		{ BPF_ALU64 | BPF_MOV | BPF_X, BPF_REG_2, BPF_REG_10, 0, 0 },
		{ BPF_ALU64 | BPF_ADD | BPF_K, BPF_REG_2, 0, 0, -4 },
		/* r1 = map (ld_imm64, pseudo map fd) */
		{ BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, map },
		{ 0, 0, 0, 0, 0 },
		{ BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem },
		{ BPF_JMP | BPF_JEQ | BPF_K, BPF_REG_0, 0, 3, 0 },
		{ BPF_ALU64 | BPF_MOV | BPF_K, BPF_REG_1, 0, 0, 1 },
		{ BPF_STX | BPF_XADD | BPF_DW, BPF_REG_0, BPF_REG_1, 0, 0 },
		{ BPF_ALU64 | BPF_MOV | BPF_K, BPF_REG_0, 0, 0, 0 },
		{ BPF_JMP | BPF_EXIT, 0, 0, 0, 0 },
	};
	static char log[4096];
	memset(&a, 0, sizeof(a));
	a.prog_type = BPF_PROG_TYPE_KPROBE;
	a.insns = (unsigned long)prog;
	a.insn_cnt = sizeof(prog) / sizeof(prog[0]);
	a.license = (unsigned long)"GPL";
	a.log_buf = (unsigned long)log;
	a.log_size = sizeof(log);
	a.log_level = 1;
	a.kern_version = 0;
	int pfd = bpf(BPF_PROG_LOAD, &a);
	if (pfd < 0) {
		fprintf(stderr, "prog load: %s\n%s\n", strerror(errno), log);
		return 1;
	}

	struct perf_event_attr pa;
	memset(&pa, 0, sizeof(pa));
	pa.type = PERF_TYPE_TRACEPOINT;
	pa.size = sizeof(pa);
	pa.config = id;
	pa.sample_period = 1;
	pa.wakeup_events = 1;
	int efd = syscall(__NR_perf_event_open, &pa, getpid(), -1, -1, 0);
	if (efd < 0) {
		perror("perf_event_open");
		return 1;
	}
	if (ioctl(efd, PERF_EVENT_IOC_SET_BPF, pfd) ||
	    ioctl(efd, PERF_EVENT_IOC_ENABLE, 0)) {
		perror("attach");
		return 1;
	}

	int s = 0;
	for (int i = 0; i < N; i++)
		s = target(s);

	unsigned int key = 0;
	unsigned long long val = 0;
	memset(&a, 0, sizeof(a));
	a.map_fd = map;
	a.key = (unsigned long)&key;
	a.value = (unsigned long)&val;
	if (bpf(BPF_MAP_LOOKUP_ELEM, &a)) {
		perror("lookup");
		return 1;
	}
	close(efd);
	wr(T "/uprobe_events", "-:uprobes/bpftest\n", 1);
	printf("calls %d, bpf hits %llu: %s\n", s, val, val == N ? "PASS" : "FAIL");
	return val != N;
}
