// rawtptest: checks the kernel's BPF raw tracepoints from the adb shell, as root.
// For each tracepoint below, a BPF_PROG_TYPE_RAW_TRACEPOINT program that adds 1 to an array map
// is attached with BPF_RAW_TRACEPOINT_OPEN; the count must grow while the test makes the
// tracepoint fire, and stop growing once the attach fd is closed. Also: an unknown tracepoint
// name gives ENOENT, and a program that reads past the tracepoint's arguments is refused at
// attach with EINVAL. The attachment is a bpf_link: pinned in /sys/fs/bpf it keeps counting after
// every fd is closed, BPF_OBJ_GET on the pin gives a link fd back, its fdinfo names it, and
// removing the pin (with no fd left) detaches it. PASS/FAIL per case, exit 1 on any failure.
#include <errno.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define INSN(c, d, s, o, i) ((struct bpf_insn){.code = (c), .dst_reg = (d), .src_reg = (s), .off = (o), .imm = (i)})

static int fails;
static char logbuf[65536];

static long sys_bpf(int cmd, union bpf_attr* a) { return syscall(__NR_bpf, cmd, a, sizeof(*a)); }

static void check(int ok, const char* name, const char* ev) {
  printf("%s %s: %s\n", ok ? "PASS" : "FAIL", name, ev);
  if (!ok) fails++;
}

static int map_create(void) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.map_type = BPF_MAP_TYPE_ARRAY;
  a.key_size = 4;
  a.value_size = 8;
  a.max_entries = 1;
  return sys_bpf(BPF_MAP_CREATE, &a);
}

static uint64_t map_read(int map) {
  uint32_t k = 0;
  uint64_t v = 0;
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.map_fd = map;
  a.key = (uintptr_t)&k;
  a.value = (uintptr_t)&v;
  sys_bpf(BPF_MAP_LOOKUP_ELEM, &a);
  return v;
}

// r1 = ctx; optionally read ctx[ctx_off]; count += 1 in map[0]; return 0
static int load_counter(int map, int ctx_off) {
  struct bpf_insn p[16];
  int n = 0;
  if (ctx_off >= 0) p[n++] = INSN(BPF_LDX | BPF_MEM | BPF_DW, BPF_REG_3, BPF_REG_1, ctx_off, 0);
  p[n++] = INSN(BPF_ST | BPF_MEM | BPF_W, BPF_REG_10, 0, -4, 0);
  p[n++] = INSN(BPF_ALU64 | BPF_MOV | BPF_X, BPF_REG_2, BPF_REG_10, 0, 0);
  p[n++] = INSN(BPF_ALU64 | BPF_ADD | BPF_K, BPF_REG_2, 0, 0, -4);
  p[n++] = INSN(BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, map);
  p[n++] = INSN(0, 0, 0, 0, 0);
  p[n++] = INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem);
  p[n++] = INSN(BPF_JMP | BPF_JEQ | BPF_K, BPF_REG_0, 0, 2, 0);
  p[n++] = INSN(BPF_ALU64 | BPF_MOV | BPF_K, BPF_REG_1, 0, 0, 1);
  p[n++] = INSN(BPF_STX | BPF_XADD | BPF_DW, BPF_REG_0, BPF_REG_1, 0, 0);
  p[n++] = INSN(BPF_ALU64 | BPF_MOV | BPF_K, BPF_REG_0, 0, 0, 0);
  p[n++] = INSN(BPF_JMP | BPF_EXIT, 0, 0, 0, 0);
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.prog_type = BPF_PROG_TYPE_RAW_TRACEPOINT;
  a.insns = (uintptr_t)p;
  a.insn_cnt = n;
  a.license = (uintptr_t) "GPL";
  a.log_buf = (uintptr_t)logbuf;
  a.log_size = sizeof(logbuf);
  a.log_level = 1;
  logbuf[0] = 0;
  return sys_bpf(BPF_PROG_LOAD, &a);
}

static int tp_open(const char* name, int prog) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.raw_tracepoint.name = (uintptr_t)name;
  a.raw_tracepoint.prog_fd = prog;
  return sys_bpf(BPF_RAW_TRACEPOINT_OPEN, &a);
}

static void fire(const char* tp) {
  if (!strcmp(tp, "sys_enter") || !strcmp(tp, "sched_switch")) {
    for (int i = 0; i < 200; i++) usleep(1000);  // syscalls and context switches
  } else if (!strcmp(tp, "sched_process_fork")) {
    for (int i = 0; i < 20; i++) {
      pid_t p = fork();
      if (p == 0) _exit(0);
      if (p > 0) waitpid(p, NULL, 0);
    }
  }
}

static void attach_case(const char* tp) {
  char name[128], ev[256];
  int map = map_create();
  int prog = load_counter(map, -1);
  snprintf(name, sizeof(name), "attach %s", tp);
  if (map < 0 || prog < 0) {
    snprintf(ev, sizeof(ev), "map %d prog %d: %s", map, prog, strerror(errno));
    check(0, name, ev);
    return;
  }
  int fd = tp_open(tp, prog);
  if (fd < 0) {
    snprintf(ev, sizeof(ev), "BPF_RAW_TRACEPOINT_OPEN: %s", strerror(errno));
    check(0, name, ev);
    close(prog);
    close(map);
    return;
  }
  uint64_t c0 = map_read(map);
  fire(tp);
  uint64_t c1 = map_read(map);
  close(fd);
  usleep(50000);
  uint64_t c2 = map_read(map);
  fire(tp);
  uint64_t c3 = map_read(map);
  snprintf(ev, sizeof(ev), "count %llu -> %llu while attached; %llu -> %llu after close",
           (unsigned long long)c0, (unsigned long long)c1, (unsigned long long)c2,
           (unsigned long long)c3);
  check(c1 > c0 && c3 == c2, name, ev);
  close(prog);
  close(map);
}

#define PIN "/sys/fs/bpf/rawtptest_link"

static int obj_pin(int fd, const char* path) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.bpf_fd = fd;
  a.pathname = (uintptr_t)path;
  return sys_bpf(BPF_OBJ_PIN, &a);
}

static int obj_get(const char* path) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.pathname = (uintptr_t)path;
  return sys_bpf(BPF_OBJ_GET, &a);
}

static void fdinfo(int fd, char* out, size_t len) {
  char path[64], buf[512];
  snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
  int f = open(path, O_RDONLY);
  ssize_t n = f < 0 ? -1 : read(f, buf, sizeof(buf) - 1);
  if (f >= 0) close(f);
  buf[n > 0 ? n : 0] = 0;
  const char* t = strstr(buf, "link_type:");
  snprintf(out, len, "%.*s", t ? (int)strcspn(t, "\n") : 4, t ? t : "none");
}

static void pin_case(void) {
  char ev[400], info[64];
  unlink(PIN);
  int map = map_create();
  int prog = load_counter(map, -1);
  int fd = tp_open("sched_switch", prog);
  if (fd < 0) {
    snprintf(ev, sizeof(ev), "attach: %s", strerror(errno));
    check(0, "pinned link", ev);
    return;
  }
  fdinfo(fd, info, sizeof(info));
  check(strstr(info, "raw_tracepoint") != NULL, "attach fd is a bpf_link", info);
  int pr = obj_pin(fd, PIN);
  snprintf(ev, sizeof(ev), "%s", pr ? strerror(errno) : "pinned at " PIN);
  check(pr == 0, "pin the link", ev);
  close(fd);
  close(prog);
  uint64_t c0 = map_read(map);
  fire("sched_switch");
  uint64_t c1 = map_read(map);
  snprintf(ev, sizeof(ev), "count %llu -> %llu with only the pin left", (unsigned long long)c0,
           (unsigned long long)c1);
  check(pr == 0 && c1 > c0, "pinned link stays attached", ev);
  int g = obj_get(PIN);
  info[0] = 0;
  if (g >= 0) fdinfo(g, info, sizeof(info));
  snprintf(ev, sizeof(ev), "%s%s", g < 0 ? strerror(errno) : "fd, ", g < 0 ? "" : info);
  check(g >= 0 && strstr(info, "raw_tracepoint") != NULL, "BPF_OBJ_GET on the pin", ev);
  if (g >= 0) close(g);
  int u = unlink(PIN);
  usleep(100000);  // the last put frees the link from a work item
  uint64_t c2 = map_read(map);
  fire("sched_switch");
  uint64_t c3 = map_read(map);
  snprintf(ev, sizeof(ev), "unlink %s; count %llu -> %llu after", u ? strerror(errno) : "ok",
           (unsigned long long)c2, (unsigned long long)c3);
  check(u == 0 && c3 == c2, "removing the pin detaches", ev);
  close(map);
}

int main(void) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  attach_case("sched_switch");
  attach_case("sys_enter");
  attach_case("sched_process_fork");
  pin_case();

  char ev[256];
  int map = map_create();
  int prog = load_counter(map, -1);
  int fd = tp_open("no_such_tracepoint", prog);
  snprintf(ev, sizeof(ev), "%s", fd < 0 ? strerror(errno) : "opened");
  check(fd < 0 && errno == ENOENT, "unknown tracepoint refused", ev);
  if (fd >= 0) close(fd);
  close(prog);

  // sched_process_fork has 2 arguments (parent, child): reading args[2] must be refused at attach
  prog = load_counter(map, 16);
  fd = prog < 0 ? -1 : tp_open("sched_process_fork", prog);
  snprintf(ev, sizeof(ev), "load %s, attach %s", prog < 0 ? strerror(errno) : "ok",
           fd < 0 ? strerror(errno) : "opened");
  check(prog >= 0 && fd < 0 && errno == EINVAL, "read past the arguments refused at attach", ev);
  if (fd >= 0) close(fd);
  if (prog >= 0) close(prog);
  prog = load_counter(map, 8);
  fd = prog < 0 ? -1 : tp_open("sched_process_fork", prog);
  snprintf(ev, sizeof(ev), "load %s, attach %s", prog < 0 ? strerror(errno) : "ok",
           fd < 0 ? strerror(errno) : "opened");
  check(prog >= 0 && fd >= 0, "read of the last argument allowed", ev);
  if (fd >= 0) close(fd);
  if (prog >= 0) close(prog);
  close(map);

  printf("rawtptest: %d failed\n", fails);
  return fails ? 1 : 0;
}
