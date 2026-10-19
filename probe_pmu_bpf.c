/* Attach BPF programs through the perf kprobe and uprobe PMUs (no tracefs),
 * check that they count every hit, and that BPF_TASK_FD_QUERY describes
 * each probe. */
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

static int pmu_type(const char *pmu)
{
	char path[128];
	int type = -1;

	snprintf(path, sizeof(path), "/sys/bus/event_source/devices/%s/type", pmu);
	FILE *f = fopen(path, "r");
	if (!f || fscanf(f, "%d", &type) != 1)
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
	if (f)
		fclose(f);
	return type;
}

/* Count hits in map[0]. */
static int load_counter(int *map)
{
	union bpf_attr a;

	memset(&a, 0, sizeof(a));
	a.map_type = BPF_MAP_TYPE_ARRAY;
	a.key_size = 4;
	a.value_size = 8;
	a.max_entries = 1;
	*map = bpf(BPF_MAP_CREATE, &a);
	if (*map < 0) {
		perror("map create");
		return -1;
	}

	struct bpf_insn prog[] = {
		{ BPF_ST | BPF_MEM | BPF_W, BPF_REG_10, 0, -4, 0 },
		{ BPF_ALU64 | BPF_MOV | BPF_X, BPF_REG_2, BPF_REG_10, 0, 0 },
		{ BPF_ALU64 | BPF_ADD | BPF_K, BPF_REG_2, 0, 0, -4 },
		{ BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, *map },
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
	int pfd = bpf(BPF_PROG_LOAD, &a);
	if (pfd < 0)
		fprintf(stderr, "prog load: %s\n%s\n", strerror(errno), log);
	return pfd;
}

static unsigned long long count(int map)
{
	unsigned int key = 0;
	unsigned long long val = 0;
	union bpf_attr a;

	memset(&a, 0, sizeof(a));
	a.map_fd = map;
	a.key = (unsigned long)&key;
	a.value = (unsigned long)&val;
	bpf(BPF_MAP_LOOKUP_ELEM, &a);
	return val;
}

static int open_pmu(int type, unsigned long config1, unsigned long config2, int pfd)
{
	struct perf_event_attr pa;

	memset(&pa, 0, sizeof(pa));
	pa.type = type;
	pa.size = sizeof(pa);
	pa.config1 = config1;	/* kprobe_func / uprobe_path */
	pa.config2 = config2;	/* probe_offset */
	int efd = syscall(__NR_perf_event_open, &pa, getpid(), -1, -1, 0);
	if (efd < 0) {
		perror("perf_event_open");
		return -1;
	}
	if (ioctl(efd, PERF_EVENT_IOC_SET_BPF, pfd) ||
	    ioctl(efd, PERF_EVENT_IOC_ENABLE, 0)) {
		perror("attach");
		return -1;
	}
	return efd;
}

static void fd_query(int efd)
{
	char buf[256] = "";
	union bpf_attr a;

	memset(&a, 0, sizeof(a));
	a.task_fd_query.pid = getpid();
	a.task_fd_query.fd = efd;
	a.task_fd_query.buf = (unsigned long)buf;
	a.task_fd_query.buf_len = sizeof(buf);
	if (bpf(BPF_TASK_FD_QUERY, &a)) {
		printf("  task_fd_query: %s\n", strerror(errno));
		return;
	}
	printf("  task_fd_query: type %u name %s offset 0x%llx\n",
	       a.task_fd_query.fd_type, buf,
	       (unsigned long long)a.task_fd_query.probe_offset);
}

int main(void)
{
	int fails = 0, map, pfd, efd, s = 0;
	unsigned long long n;

	/* kprobe PMU on a syscall path we can trigger N times */
	pfd = load_counter(&map);
	efd = open_pmu(pmu_type("kprobe"), (unsigned long)"do_sys_open", 0, pfd);
	if (pfd < 0 || efd < 0)
		return 1;
	for (int i = 0; i < N; i++)
		close(open("/dev/null", O_RDONLY));
	n = count(map);
	printf("kprobe PMU: %d opens, bpf hits %llu: %s\n", N, n,
	       n >= N ? "PASS" : "FAIL");
	fails += n < N;
	fd_query(efd);
	close(efd);

	/* uprobe PMU on our own function */
	char exe[256], line[512];
	unsigned long start, end, pgoff, off = 0, addr = (unsigned long)target;
	FILE *f = fopen("/proc/self/maps", "r");
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &pgoff) == 3 &&
		    addr >= start && addr < end) {
			off = addr - start + pgoff;
			break;
		}
	fclose(f);
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	exe[len] = 0;

	pfd = load_counter(&map);
	efd = open_pmu(pmu_type("uprobe"), (unsigned long)exe, off, pfd);
	if (pfd < 0 || efd < 0)
		return 1;
	for (int i = 0; i < N; i++)
		s = target(s);
	n = count(map);
	printf("uprobe PMU: %d calls, bpf hits %llu: %s\n", s, n,
	       n == N ? "PASS" : "FAIL");
	fails += n != N;
	fd_query(efd);
	close(efd);
	return fails;
}
