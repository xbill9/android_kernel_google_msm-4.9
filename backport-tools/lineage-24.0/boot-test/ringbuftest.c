// ringbuftest: checks the BPF ring buffer backport (BPF_MAP_TYPE_RINGBUF and the
// bpf_ringbuf_* helpers) from the adb shell as root. A socket filter program run
// with BPF_PROG_TEST_RUN writes records; the test reads them back through mmap()
// and epoll like libbpf's consumer does, and checks that the verifier rejects
// programs that misuse a reservation. PASS/FAIL per case, exit 1 on any failure.
#include <errno.h>
#include <linux/bpf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PAGE 4096
#define RB_SIZE (4 * PAGE)
#define MAGIC 12345

static int fails;
static char logbuf[65536];

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

static long sys_bpf(int cmd, union bpf_attr* a) { return syscall(__NR_bpf, cmd, a, sizeof(*a)); }

static int map_create(int type, int key, int value, int entries) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.map_type = type;
  a.key_size = key;
  a.value_size = value;
  a.max_entries = entries;
  return sys_bpf(BPF_MAP_CREATE, &a);
}

// Instruction encoders.
#define INSN(c, d, s, o, i) ((struct bpf_insn){.code = (c), .dst_reg = (d), .src_reg = (s), .off = (o), .imm = (i)})
#define MOV64_IMM(d, i) INSN(BPF_ALU64 | BPF_MOV | BPF_K, d, 0, 0, i)
#define MOV64_REG(d, s) INSN(BPF_ALU64 | BPF_MOV | BPF_X, d, s, 0, 0)
#define ADD64_IMM(d, i) INSN(BPF_ALU64 | BPF_ADD | BPF_K, d, 0, 0, i)
#define LD_MAP_FD(d, fd) INSN(BPF_LD | BPF_DW | BPF_IMM, d, BPF_PSEUDO_MAP_FD, 0, fd), INSN(0, 0, 0, 0, 0)
#define CALL(f) INSN(BPF_JMP | BPF_CALL, 0, 0, 0, f)
#define EXIT() INSN(BPF_JMP | BPF_EXIT, 0, 0, 0, 0)
#define JEQ_IMM(d, i, o) INSN(BPF_JMP | BPF_JEQ | BPF_K, d, 0, o, i)
#define ST_DW(d, o, i) INSN(BPF_ST | BPF_MEM | BPF_DW, d, 0, o, i)
#define STX_DW(d, s, o) INSN(BPF_STX | BPF_MEM | BPF_DW, d, s, o, 0)
#define LDX_W(d, s, o) INSN(BPF_LDX | BPF_MEM | BPF_W, d, s, o, 0)

static int prog_load(const struct bpf_insn* insns, int n, int with_log) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
  a.insns = (uintptr_t)insns;
  a.insn_cnt = n;
  a.license = (uintptr_t) "GPL";
  logbuf[0] = 0;
  if (with_log) {
    a.log_buf = (uintptr_t)logbuf;
    a.log_size = sizeof(logbuf);
    a.log_level = 1;
  }
  return sys_bpf(BPF_PROG_LOAD, &a);
}

static int prog_run(int fd, uint32_t* retval) {
  static char pkt[64];
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.test.prog_fd = fd;
  a.test.data_in = (uintptr_t)pkt;
  a.test.data_size_in = sizeof(pkt);
  a.test.repeat = 1;
  int r = sys_bpf(BPF_PROG_TEST_RUN, &a);
  *retval = a.test.retval;
  return r;
}

// Load must fail, and the verifier log must contain want.
static void reject(const char* name, const struct bpf_insn* insns, int n, const char* want) {
  int fd = prog_load(insns, n, 1);
  const char* last = strrchr(logbuf, '\n');
  // print the verifier's last complete line as evidence
  char line[200] = "";
  if (last) {
    const char* p = last;
    while (p > logbuf && p[-1] != '\n') p--;
    if (p == last && last > logbuf) {
      const char* q = last - 1;
      while (q > logbuf && q[-1] != '\n') q--;
      p = q;
    }
    snprintf(line, sizeof(line), "%.*s", (int)(strchr(p, '\n') ? strchr(p, '\n') - p : 150), p);
  }
  check(fd < 0 && strstr(logbuf, want) != NULL, name, "load %s; want \"%s\"; log: %s",
        fd < 0 ? "rejected" : "ACCEPTED", want, line);
  if (fd >= 0) close(fd);
}

int main(void) {
  printf("ringbuftest, uid %d\n", getuid());

  // Map creation rules.
  int bad = map_create(BPF_MAP_TYPE_RINGBUF, 0, 0, 3 * PAGE);
  check(bad < 0 && errno == EINVAL, "create: size not a power of 2", "fd %d errno %d", bad, errno);
  bad = map_create(BPF_MAP_TYPE_RINGBUF, 4, 0, RB_SIZE);
  check(bad < 0 && errno == EINVAL, "create: key_size set", "fd %d errno %d", bad, errno);
  bad = map_create(BPF_MAP_TYPE_RINGBUF, 0, 0, 1024);
  check(bad < 0 && errno == EINVAL, "create: smaller than a page", "fd %d errno %d", bad, errno);
  bad = map_create(26 /* BPF_MAP_TYPE_STRUCT_OPS */, 4, 4, 1);
  check(bad < 0 && errno == EINVAL, "create: STRUCT_OPS (number only)", "fd %d errno %d", bad, errno);

  int rb = map_create(BPF_MAP_TYPE_RINGBUF, 0, 0, RB_SIZE);
  check(rb >= 0, "create ringbuf", "fd %d errno %d", rb, rb < 0 ? errno : 0);
  if (rb < 0) return 1;
  int arr = map_create(BPF_MAP_TYPE_ARRAY, 4, 8, 1);

  // mmap rules: consumer page writable, producer page and data read-only.
  void* cons = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, rb, 0);
  check(cons != MAP_FAILED, "mmap consumer page rw", "%s", cons == MAP_FAILED ? strerror(errno) : "ok");
  void* prod = mmap(NULL, PAGE + 2 * RB_SIZE, PROT_READ, MAP_SHARED, rb, PAGE);
  check(prod != MAP_FAILED, "mmap producer + data ro", "%s", prod == MAP_FAILED ? strerror(errno) : "ok");
  void* w = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, rb, PAGE);
  check(w == MAP_FAILED && errno == EPERM, "mmap producer page rw refused", "%s",
        w == MAP_FAILED ? strerror(errno) : "MAPPED");
  if (w != MAP_FAILED) munmap(w, PAGE);
  w = mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE, rb, 0);
  check(w == MAP_FAILED && errno == EINVAL, "mmap MAP_PRIVATE refused", "%s",
        w == MAP_FAILED ? strerror(errno) : "MAPPED");
  if (w != MAP_FAILED) munmap(w, PAGE);
  w = mmap(NULL, PAGE + 2 * RB_SIZE + PAGE, PROT_READ, MAP_SHARED, rb, PAGE);
  check(w == MAP_FAILED, "mmap past the end refused", "%s", w == MAP_FAILED ? strerror(errno) : "MAPPED");
  if (w != MAP_FAILED) munmap(w, PAGE + 2 * RB_SIZE + PAGE);
  if (prod != MAP_FAILED) {
    check(mprotect(prod, PAGE, PROT_READ | PROT_WRITE) != 0, "mprotect producer page rw refused", "errno %d",
          errno);
  }
  if (cons == MAP_FAILED || prod == MAP_FAILED) return 1;
  volatile unsigned long* cons_pos = cons;
  volatile unsigned long* prod_pos = prod;
  volatile unsigned char* data = (unsigned char*)prod + PAGE;

  int ep = epoll_create1(0);
  struct epoll_event ev = {.events = EPOLLIN};
  check(epoll_ctl(ep, EPOLL_CTL_ADD, rb, &ev) == 0, "epoll_ctl add", "errno %d", errno);
  struct epoll_event got;
  int n = epoll_wait(ep, &got, 1, 0);
  check(n == 0, "epoll: empty ring not ready", "%d events", n);

  // reserve 8 bytes, store MAGIC, submit
  struct bpf_insn reserve_submit[] = {
      LD_MAP_FD(BPF_REG_1, rb),
      MOV64_IMM(BPF_REG_2, 8),
      MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve),
      JEQ_IMM(BPF_REG_0, 0, 6),
      ST_DW(BPF_REG_0, 0, MAGIC),
      MOV64_REG(BPF_REG_1, BPF_REG_0),
      MOV64_IMM(BPF_REG_2, 0),
      CALL(BPF_FUNC_ringbuf_submit),
      MOV64_IMM(BPF_REG_0, 0),
      EXIT(),
      MOV64_IMM(BPF_REG_0, 1),
      EXIT(),
  };
  int p1 = prog_load(reserve_submit, sizeof(reserve_submit) / sizeof(reserve_submit[0]), 1);
  check(p1 >= 0, "load reserve+submit", "fd %d errno %d %s", p1, p1 < 0 ? errno : 0, p1 < 0 ? logbuf : "");
  uint32_t ret = 99;
  if (p1 >= 0) {
    int r = prog_run(p1, &ret);
    check(r == 0 && ret == 0, "run reserve+submit", "r %d retval %u", r, ret);
    n = epoll_wait(ep, &got, 1, 1000);
    check(n == 1 && (got.events & EPOLLIN), "epoll: record wakes the consumer", "%d events", n);
    unsigned long pp = *prod_pos;
    uint32_t len = *(volatile uint32_t*)data;
    uint64_t val = *(volatile uint64_t*)(data + 8);
    check(pp == 16 && len == 8 && val == MAGIC, "consumer reads the record", "producer_pos %lu len %u value %llu",
          pp, len, (unsigned long long)val);
    *cons_pos = pp;
  }

  // ringbuf_output from the stack, then ringbuf_query of available data
  struct bpf_insn output[] = {
      ST_DW(BPF_REG_10, -8, MAGIC + 1),
      LD_MAP_FD(BPF_REG_1, rb),
      MOV64_REG(BPF_REG_2, BPF_REG_10),
      ADD64_IMM(BPF_REG_2, -8),
      MOV64_IMM(BPF_REG_3, 8),
      MOV64_IMM(BPF_REG_4, 0),
      CALL(BPF_FUNC_ringbuf_output),
      EXIT(),
  };
  int p2 = prog_load(output, sizeof(output) / sizeof(output[0]), 1);
  check(p2 >= 0, "load ringbuf_output", "fd %d errno %d %s", p2, p2 < 0 ? errno : 0, p2 < 0 ? logbuf : "");
  struct bpf_insn query[] = {
      LD_MAP_FD(BPF_REG_1, rb),
      MOV64_IMM(BPF_REG_2, BPF_RB_AVAIL_DATA),
      CALL(BPF_FUNC_ringbuf_query),
      EXIT(),
  };
  int p3 = prog_load(query, sizeof(query) / sizeof(query[0]), 1);
  check(p3 >= 0, "load ringbuf_query", "fd %d errno %d", p3, p3 < 0 ? errno : 0);
  if (p2 >= 0 && p3 >= 0) {
    int r = prog_run(p2, &ret);
    check(r == 0 && ret == 0, "run ringbuf_output", "r %d retval %u", r, ret);
    uint64_t val = *(volatile uint64_t*)(data + 16 + 8);
    check(*prod_pos == 32 && val == MAGIC + 1, "consumer reads output record", "producer_pos %lu value %llu",
          *prod_pos, (unsigned long long)val);
    r = prog_run(p3, &ret);
    check(r == 0 && ret == 16, "query AVAIL_DATA", "r %d retval %u (want 16)", r, ret);
    *cons_pos = *prod_pos;
    r = prog_run(p3, &ret);
    check(r == 0 && ret == 0, "query after consuming", "retval %u (want 0)", ret);
  }

  // Wrap-around: 24-byte records do not divide the ring, so some straddle its
  // end; the double mapping of the data pages must show them contiguously.
  struct bpf_insn reserve16[] = {
      LD_MAP_FD(BPF_REG_1, rb),
      MOV64_IMM(BPF_REG_2, 16),
      MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve),
      JEQ_IMM(BPF_REG_0, 0, 7),
      ST_DW(BPF_REG_0, 0, MAGIC),
      ST_DW(BPF_REG_0, 8, MAGIC + 2),
      MOV64_REG(BPF_REG_1, BPF_REG_0),
      MOV64_IMM(BPF_REG_2, 0),
      CALL(BPF_FUNC_ringbuf_submit),
      MOV64_IMM(BPF_REG_0, 0),
      EXIT(),
      MOV64_IMM(BPF_REG_0, 1),
      EXIT(),
  };
  int p5 = prog_load(reserve16, sizeof(reserve16) / sizeof(reserve16[0]), 1);
  check(p5 >= 0, "load reserve 16", "fd %d %s", p5, p5 < 0 ? logbuf : "");
  if (p5 >= 0) {
    int ok = 1, runs = 0, straddled = 0;
    for (int i = 0; i < 2 * RB_SIZE / 24; i++) {
      prog_run(p5, &ret);
      runs++;
      unsigned long c = *cons_pos;
      unsigned long o = c & (RB_SIZE - 1);
      uint32_t len = *(volatile uint32_t*)(data + o);
      uint64_t v1 = *(volatile uint64_t*)(data + o + 8);
      uint64_t v2 = *(volatile uint64_t*)(data + o + 16);
      if (o + 24 > RB_SIZE) straddled++;
      if (ret != 0 || len != 16 || v1 != MAGIC || v2 != MAGIC + 2) ok = 0;
      *cons_pos = c + 24;
    }
    check(ok && straddled > 0, "records across wrap-around", "%d records, %d straddle the end, producer_pos %lu",
          runs, straddled, *prod_pos);
    close(p5);
  }
  if (p1 >= 0) {
    // without consuming, the ring fills and reserve returns NULL (retval 1)
    int full = 0;
    for (int i = 0; i < RB_SIZE / 16 + 2; i++) {
      prog_run(p1, &ret);
      if (ret == 1) full++;
    }
    check(full >= 1, "full ring: reserve returns NULL", "%d of %d runs got NULL", full, RB_SIZE / 16 + 2);
    *cons_pos = *prod_pos;
  }

  // reserve larger than the ring returns NULL at run time
  struct bpf_insn too_big[] = {
      LD_MAP_FD(BPF_REG_1, rb),
      MOV64_IMM(BPF_REG_2, RB_SIZE + 8),
      MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve),
      JEQ_IMM(BPF_REG_0, 0, 3),
      MOV64_REG(BPF_REG_1, BPF_REG_0),
      MOV64_IMM(BPF_REG_2, 0),
      CALL(BPF_FUNC_ringbuf_discard),
      MOV64_IMM(BPF_REG_0, 0),
      EXIT(),
  };
  int p4 = prog_load(too_big, sizeof(too_big) / sizeof(too_big[0]), 1);
  if (p4 >= 0) {
    // JEQ jumps to EXIT with r0 == 0; a non-NULL result would also exit 0, so
    // look at producer_pos: it must not move.
    unsigned long before = *prod_pos;
    prog_run(p4, &ret);
    check(*prod_pos == before, "reserve larger than ring", "producer_pos %lu -> %lu", before, *prod_pos);
    close(p4);
  } else {
    check(0, "reserve larger than ring", "load failed: %s", logbuf);
  }

  // The verifier must reject misuse.
  struct bpf_insn no_submit[] = {
      LD_MAP_FD(BPF_REG_1, rb), MOV64_IMM(BPF_REG_2, 8), MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve), MOV64_IMM(BPF_REG_0, 0), EXIT(),
  };
  reject("reject: reservation never submitted", no_submit, sizeof(no_submit) / sizeof(no_submit[0]), "Unreleased reference");

  struct bpf_insn off_submit[] = {
      LD_MAP_FD(BPF_REG_1, rb), MOV64_IMM(BPF_REG_2, 16), MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve), JEQ_IMM(BPF_REG_0, 0, 4), MOV64_REG(BPF_REG_1, BPF_REG_0),
      ADD64_IMM(BPF_REG_1, 8), MOV64_IMM(BPF_REG_2, 0), CALL(BPF_FUNC_ringbuf_submit),
      MOV64_IMM(BPF_REG_0, 0), EXIT(),
  };
  reject("reject: submit of an offset pointer", off_submit, sizeof(off_submit) / sizeof(off_submit[0]), "dereference of modified mem ptr");

  struct bpf_insn oob[] = {
      LD_MAP_FD(BPF_REG_1, rb), MOV64_IMM(BPF_REG_2, 8), MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve), JEQ_IMM(BPF_REG_0, 0, 4), ST_DW(BPF_REG_0, 8, 1),
      MOV64_REG(BPF_REG_1, BPF_REG_0), MOV64_IMM(BPF_REG_2, 0), CALL(BPF_FUNC_ringbuf_submit),
      MOV64_IMM(BPF_REG_0, 0), EXIT(),
  };
  reject("reject: write past the reservation", oob, sizeof(oob) / sizeof(oob[0]), "invalid access to memory");

  struct bpf_insn var_size[] = {
      LDX_W(BPF_REG_2, BPF_REG_1, 0),  // skb->len, unknown at load time
      LD_MAP_FD(BPF_REG_1, rb), MOV64_IMM(BPF_REG_3, 0), CALL(BPF_FUNC_ringbuf_reserve),
      JEQ_IMM(BPF_REG_0, 0, 3), MOV64_REG(BPF_REG_1, BPF_REG_0), MOV64_IMM(BPF_REG_2, 0),
      CALL(BPF_FUNC_ringbuf_submit), MOV64_IMM(BPF_REG_0, 0), EXIT(),
  };
  reject("reject: size not a constant", var_size, sizeof(var_size) / sizeof(var_size[0]), "unbounded size");

  struct bpf_insn null_arith[] = {
      LD_MAP_FD(BPF_REG_1, rb), MOV64_IMM(BPF_REG_2, 8), MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve), ADD64_IMM(BPF_REG_0, 8), MOV64_IMM(BPF_REG_0, 0), EXIT(),
  };
  reject("reject: arithmetic before the NULL check", null_arith, sizeof(null_arith) / sizeof(null_arith[0]), "pointer arithmetic on mem_or_null prohibited");

  struct bpf_insn use_after[] = {
      LD_MAP_FD(BPF_REG_1, rb), MOV64_IMM(BPF_REG_2, 8), MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve), JEQ_IMM(BPF_REG_0, 0, 5), MOV64_REG(BPF_REG_6, BPF_REG_0),
      MOV64_REG(BPF_REG_1, BPF_REG_0), MOV64_IMM(BPF_REG_2, 0), CALL(BPF_FUNC_ringbuf_submit),
      ST_DW(BPF_REG_6, 0, 1), MOV64_IMM(BPF_REG_0, 0), EXIT(),
  };
  reject("reject: write after submit", use_after, sizeof(use_after) / sizeof(use_after[0]), "R6 invalid mem access");

  struct bpf_insn twice[] = {
      LD_MAP_FD(BPF_REG_1, rb), MOV64_IMM(BPF_REG_2, 8), MOV64_IMM(BPF_REG_3, 0),
      CALL(BPF_FUNC_ringbuf_reserve), JEQ_IMM(BPF_REG_0, 0, 7), MOV64_REG(BPF_REG_6, BPF_REG_0),
      MOV64_REG(BPF_REG_1, BPF_REG_0), MOV64_IMM(BPF_REG_2, 0), CALL(BPF_FUNC_ringbuf_submit),
      MOV64_REG(BPF_REG_1, BPF_REG_6), MOV64_IMM(BPF_REG_2, 0), CALL(BPF_FUNC_ringbuf_discard),
      MOV64_IMM(BPF_REG_0, 0), EXIT(),
  };
  reject("reject: submit then discard", twice, sizeof(twice) / sizeof(twice[0]), "expected=mem");

  if (arr >= 0) {
    struct bpf_insn wrong_map[] = {
        LD_MAP_FD(BPF_REG_1, arr), MOV64_IMM(BPF_REG_2, 8), MOV64_IMM(BPF_REG_3, 0),
        CALL(BPF_FUNC_ringbuf_reserve), JEQ_IMM(BPF_REG_0, 0, 3), MOV64_REG(BPF_REG_1, BPF_REG_0),
        MOV64_IMM(BPF_REG_2, 0), CALL(BPF_FUNC_ringbuf_submit), MOV64_IMM(BPF_REG_0, 0), EXIT(),
    };
    reject("reject: reserve on an array map", wrong_map, sizeof(wrong_map) / sizeof(wrong_map[0]), "cannot pass map_type 2 into func bpf_ringbuf_reserve");

    struct bpf_insn stack_submit[] = {
        ST_DW(BPF_REG_10, -8, 0), MOV64_REG(BPF_REG_1, BPF_REG_10), ADD64_IMM(BPF_REG_1, -8),
        MOV64_IMM(BPF_REG_2, 0), CALL(BPF_FUNC_ringbuf_submit), MOV64_IMM(BPF_REG_0, 0), EXIT(),
    };
    reject("reject: submit of a stack pointer", stack_submit, sizeof(stack_submit) / sizeof(stack_submit[0]), "type=fp expected=mem");
  }

  printf("%d failed\n", fails);
  return fails ? 1 : 0;
}
