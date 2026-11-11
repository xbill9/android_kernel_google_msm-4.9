// atomictest: checks the kernel's BPF atomic instructions (BPF_ATOMIC with BPF_ADD, BPF_AND,
// BPF_OR, BPF_XOR, their BPF_FETCH forms, BPF_XCHG and BPF_CMPXCHG) from the adb shell, as root.
// Runs linux v6.1's verifier/atomic_*.c cases (atomictest-cases.inc) the way test_verifier does:
// each socket-filter case is loaded as root and, unless it must be rejected, run once with
// BPF_PROG_TEST_RUN and must return 0; then it is loaded again as an unprivileged uid and must
// give the unprivileged result. A REJECT must leave the expected text in the verifier log.
// Extra cases: precision tracking through an atomic fetch (linux 179ee84a8911), and a
// 10,000-iteration loop of each op checked against the C result; the loop programs must be
// JIT-compiled (bpf_prog_info.jited_prog_len > 0), so the arm64 JIT is what runs them.
// "atomictest interp" sets net.core.bpf_jit_enable to 0 for the run (and back to 1 after), so
// every case runs in the interpreter instead, and the loop programs must not be JIT-compiled.
// PASS/FAIL/SKIP per case, exit 1 on any failure.
#include <errno.h>
#include <linux/bpf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define UNPRIV_UID 9999  // AID_NOBODY

// test_verifier's instruction macros (tools/include/linux/filter.h).
#define BPF_RAW_INSN(CODE, DST, SRC, OFF, IMM) \
  ((struct bpf_insn){.code = CODE, .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = IMM})
#define BPF_ALU64_IMM(OP, DST, IMM) BPF_RAW_INSN(BPF_ALU64 | BPF_OP(OP) | BPF_K, DST, 0, 0, IMM)
#define BPF_MOV64_IMM(DST, IMM) BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_K, DST, 0, 0, IMM)
#define BPF_MOV32_IMM(DST, IMM) BPF_RAW_INSN(BPF_ALU | BPF_MOV | BPF_K, DST, 0, 0, IMM)
#define BPF_MOV64_REG(DST, SRC) BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_X, DST, SRC, 0, 0)
#define BPF_MOV32_REG(DST, SRC) BPF_RAW_INSN(BPF_ALU | BPF_MOV | BPF_X, DST, SRC, 0, 0)
#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) BPF_RAW_INSN(BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM, DST, SRC, OFF, 0)
#define BPF_STX_MEM(SIZE, DST, SRC, OFF) BPF_RAW_INSN(BPF_STX | BPF_SIZE(SIZE) | BPF_MEM, DST, SRC, OFF, 0)
#define BPF_ST_MEM(SIZE, DST, OFF, IMM) BPF_RAW_INSN(BPF_ST | BPF_SIZE(SIZE) | BPF_MEM, DST, 0, OFF, IMM)
#define BPF_ATOMIC_OP(SIZE, OP, DST, SRC, OFF) \
  BPF_RAW_INSN(BPF_STX | BPF_SIZE(SIZE) | BPF_ATOMIC, DST, SRC, OFF, OP)
#define BPF_JMP_IMM(OP, DST, IMM, OFF) BPF_RAW_INSN(BPF_JMP | BPF_OP(OP) | BPF_K, DST, 0, OFF, IMM)
#define BPF_JMP_REG(OP, DST, SRC, OFF) BPF_RAW_INSN(BPF_JMP | BPF_OP(OP) | BPF_X, DST, SRC, OFF, 0)
#define BPF_JMP32_IMM(OP, DST, IMM, OFF) BPF_RAW_INSN(BPF_JMP32 | BPF_OP(OP) | BPF_K, DST, 0, OFF, IMM)
#define BPF_EXIT_INSN() BPF_RAW_INSN(BPF_JMP | BPF_EXIT, 0, 0, 0, 0)
#define BPF_ALU64_REG(OP, DST, SRC) BPF_RAW_INSN(BPF_ALU64 | BPF_OP(OP) | BPF_X, DST, SRC, 0, 0)
#define BPF_LD_IMM64_RAW(DST, SRC, IMM) \
  BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, DST, SRC, 0, (uint32_t)(IMM)), \
  BPF_RAW_INSN(0, 0, 0, 0, ((uint64_t)(IMM)) >> 32)
#define BPF_LD_IMM64(DST, IMM) BPF_LD_IMM64_RAW(DST, 0, IMM)
#define BPF_LD_MAP_FD(DST, MAP_FD) BPF_LD_IMM64_RAW(DST, BPF_PSEUDO_MAP_FD, MAP_FD)
#define BPF_EMIT_CALL(FUNC) BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, FUNC)

#define MAX_INSNS 64
enum { UNDEF, ACCEPT, REJECT };

// The subset of test_verifier's struct bpf_test that the atomic cases use.
struct bpf_test {
  const char* descr;
  struct bpf_insn insns[MAX_INSNS];
  const char* errstr;
  const char* errstr_unpriv;
  int result;
  int result_unpriv;
  int prog_type;
  int expected_attach_type;
  const char* kfunc;
  int fixup_map_array_48b[4];  // insn indexes whose imm gets an array map fd (48-byte values)
};

static struct bpf_test tests[] = {
#include "atomictest-cases.inc"
};

static int fails, passes, skips;
static char logbuf[1 << 20];

static long sys_bpf(int cmd, union bpf_attr* a) { return syscall(__NR_bpf, cmd, a, sizeof(*a)); }

static int insn_count(const struct bpf_insn* insns) {
  int n = MAX_INSNS;
  while (n > 0 && !insns[n - 1].code && !insns[n - 1].dst_reg && !insns[n - 1].src_reg &&
         !insns[n - 1].off && !insns[n - 1].imm)
    n--;
  return n;
}

static int prog_load(const struct bpf_insn* insns, int n, int log_level) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
  a.insns = (uintptr_t)insns;
  a.insn_cnt = n;
  a.license = (uintptr_t) "GPL";
  logbuf[0] = 0;
  a.log_buf = (uintptr_t)logbuf;
  a.log_size = sizeof(logbuf);
  a.log_level = log_level;
  int fd = sys_bpf(BPF_PROG_LOAD, &a);
  if (fd < 0 && errno == ENOSPC) {  // log too long for level 2; retry with level 1
    a.log_level = 1;
    logbuf[0] = 0;
    fd = sys_bpf(BPF_PROG_LOAD, &a);
  }
  return fd;
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

static void last_line(char* out, size_t len) {
  size_t l = strlen(logbuf);
  while (l > 0 && logbuf[l - 1] == '\n') l--;
  size_t s = l;
  while (s > 0 && logbuf[s - 1] != '\n') s--;
  // skip the "processed N insns" summary line when there is one
  if (!strncmp(logbuf + s, "processed ", 10) && s > 0) {
    l = s - 1;
    s = l;
    while (s > 0 && logbuf[s - 1] != '\n') s--;
  }
  snprintf(out, len, "%.*s", (int)(l - s), logbuf + s);
}

// The verifier log names an unknown value's type 'inv' up to linux 5.17 and 'scalar' from
// 7df5072cc05f (5.18) on; the v6.1 cases expect 'scalar'. True if the log has want, in either
// wording.
static int log_has(const char* want) {
  if (strstr(logbuf, want)) return 1;
  char old[256];
  const char* p = strstr(want, "'scalar'");
  if (!p || strlen(want) >= sizeof(old)) return 0;
  snprintf(old, sizeof(old), "%.*s'inv'%s", (int)(p - want), want, p + strlen("'scalar'"));
  return strstr(logbuf, old) != NULL;
}

// Loads (and for an accepted root load, runs) one case. Returns NULL on success or a reason.
static int map48 = -1;

static const char* run_case(const struct bpf_test* t, int unpriv, char* ev, size_t evlen) {
  static char why[512];
  static struct bpf_insn insns[MAX_INSNS];
  int n = insn_count(t->insns);
  memcpy(insns, t->insns, sizeof(insns));
  for (int k = 0; k < 4 && t->fixup_map_array_48b[k]; k++) insns[t->fixup_map_array_48b[k]].imm = map48;
  int want = unpriv && t->result_unpriv != UNDEF ? t->result_unpriv : t->result;
  const char* want_err = unpriv && t->errstr_unpriv ? t->errstr_unpriv : t->errstr;
  int fd = prog_load(insns, n, 1);
  int err = errno;
  last_line(ev, evlen);
  if (want == REJECT) {
    if (fd >= 0) {
      close(fd);
      return "loaded, want REJECT";
    }
    if (want_err && !log_has(want_err)) {
      snprintf(why, sizeof(why), "rejected (%s), log lacks \"%s\"", strerror(err), want_err);
      return why;
    }
    return NULL;
  }
  if (fd < 0) {
    snprintf(why, sizeof(why), "rejected (%s), want ACCEPT", strerror(err));
    return why;
  }
  if (!unpriv) {
    uint32_t ret = 0;
    if (prog_run(fd, &ret)) {
      snprintf(why, sizeof(why), "BPF_PROG_TEST_RUN: %s", strerror(errno));
      close(fd);
      return why;
    }
    snprintf(ev, evlen, "retval %u", ret);
    if (ret != 0) {
      snprintf(why, sizeof(why), "retval %u, want 0", ret);
      close(fd);
      return why;
    }
  }
  close(fd);
  return NULL;
}

static void report(const char* name, const char* why, const char* ev) {
  if (why) {
    fails++;
    printf("FAIL %s: %s [%s]\n", name, why, ev);
  } else {
    passes++;
    printf("PASS %s: %s\n", name, ev);
  }
}

// One case as the unprivileged uid, in a child process. 0 pass, 1 fail.
static int run_unpriv(const struct bpf_test* t, char* line, size_t len) {
  int p[2];
  if (pipe(p)) return 1;
  pid_t pid = fork();
  if (pid == 0) {
    close(p[0]);
    char ev[256] = "";
    if (setgid(UNPRIV_UID) || setuid(UNPRIV_UID)) {
      dprintf(p[1], "setuid: %s", strerror(errno));
      _exit(1);
    }
    const char* why = run_case(t, 1, ev, sizeof(ev));
    if (why)
      dprintf(p[1], "%s [%s]", why, ev);
    else
      dprintf(p[1], "%s", ev[0] ? ev : "ok");
    _exit(why ? 1 : 0);
  }
  close(p[1]);
  ssize_t r = read(p[0], line, len - 1);
  line[r > 0 ? r : 0] = 0;
  close(p[0]);
  int st;
  waitpid(pid, &st, 0);
  return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

// Precision through an atomic fetch: the example from linux 179ee84a8911. r2 receives the
// stack slot's value through atomic64_fetch_add and is then added to a pointer, so the
// backtracking must carry precision from r2 to fp-8 and on to r1.
static void precision_case(void) {
  struct bpf_insn insns[] = {
      BPF_MOV64_IMM(BPF_REG_1, 8),
      BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8),
      BPF_MOV64_IMM(BPF_REG_2, 0),
      BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_2, -8),
      BPF_MOV64_REG(BPF_REG_3, BPF_REG_10),
      BPF_ALU64_REG(BPF_ADD, BPF_REG_3, BPF_REG_2),
      BPF_MOV64_IMM(BPF_REG_0, 0),
      BPF_EXIT_INSN(),
  };
  int fd = prog_load(insns, sizeof(insns) / sizeof(insns[0]), 2);
  // 4.9's backtracking log: "regs=<mask> stack=<mask> before <idx>: ..."
  int slot = strstr(logbuf, "regs=0 stack=1 before 2:") != NULL;
  int r1 = strstr(logbuf, "regs=2 stack=0 before 0:") != NULL;
  char why[160];
  snprintf(why, sizeof(why), "load %s, fp-8 precise before insn 2: %s, r1 precise before insn 0: %s",
           fd >= 0 ? "ok" : strerror(errno), slot ? "yes" : "no", r1 ? "yes" : "no");
  if (fd >= 0 && slot && r1) {
    passes++;
    printf("PASS precision through atomic64_fetch_add: %s\n", why);
  } else {
    fails++;
    printf("FAIL precision through atomic64_fetch_add: %s\n", why);
  }
  if (fd >= 0) close(fd);
}

// Each op 10,000 times in a bounded loop is not expressible on 4.9 (no bounded loops), so the
// loop is unrolled 16 times per program and the program is run with repeat = 625: the map value
// must end at the value C computes.
// Bytes of arm64 code the JIT produced for a program, 0 if it runs in the interpreter.
static unsigned jited_len(int fd) {
  struct bpf_prog_info info;
  union bpf_attr a;
  memset(&info, 0, sizeof(info));
  memset(&a, 0, sizeof(a));
  a.info.bpf_fd = fd;
  a.info.info_len = sizeof(info);
  a.info.info = (uintptr_t)&info;
  if (sys_bpf(BPF_OBJ_GET_INFO_BY_FD, &a)) return 0;
  return info.jited_prog_len;
}

static int interp;

static void stress_case(const char* name, int size, int op, uint64_t init, uint64_t operand) {
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.map_type = BPF_MAP_TYPE_ARRAY;
  a.key_size = 4;
  a.value_size = 8;
  a.max_entries = 1;
  int map = sys_bpf(BPF_MAP_CREATE, &a);
  if (map < 0) {
    fails++;
    printf("FAIL %s: map create: %s\n", name, strerror(errno));
    return;
  }
  uint32_t key = 0;
  uint64_t v = init;
  memset(&a, 0, sizeof(a));
  a.map_fd = map;
  a.key = (uintptr_t)&key;
  a.value = (uintptr_t)&v;
  sys_bpf(BPF_MAP_UPDATE_ELEM, &a);

  struct bpf_insn p[128];  // 10 + 16 * 4 + 2 at most
  int n = 0;
  p[n++] = BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0);
  p[n++] = BPF_MOV64_REG(BPF_REG_2, BPF_REG_10);
  p[n++] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4);
  p[n++] = BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, map);
  p[n++] = BPF_RAW_INSN(0, 0, 0, 0, 0);
  p[n++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem);
  p[n++] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2);
  p[n++] = BPF_MOV64_IMM(BPF_REG_0, 1);
  p[n++] = BPF_EXIT_INSN();
  p[n++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_0);
  for (int i = 0; i < 16; i++) {
    p[n++] = BPF_MOV64_IMM(BPF_REG_1, (int32_t)operand);
    if (op == BPF_CMPXCHG) {
      // r0 = current value (fetched), then cmpxchg(r0 -> r0 + operand): always succeeds
      p[n++] = BPF_LDX_MEM(size, BPF_REG_0, BPF_REG_6, 0);
      p[n++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_0);
      p[n++] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, (int32_t)operand);
    }
    p[n++] = BPF_ATOMIC_OP(size, op, BPF_REG_6, BPF_REG_1, 0);
  }
  p[n++] = BPF_MOV64_IMM(BPF_REG_0, 0);
  p[n++] = BPF_EXIT_INSN();
  int fd = prog_load(p, n, 1);
  if (fd < 0) {
    char ev[256];
    last_line(ev, sizeof(ev));
    fails++;
    printf("FAIL %s: load: %s [%s]\n", name, strerror(errno), ev);
    close(map);
    return;
  }
  static char pkt[64];
  memset(&a, 0, sizeof(a));
  a.test.prog_fd = fd;
  a.test.data_in = (uintptr_t)pkt;
  a.test.data_size_in = sizeof(pkt);
  a.test.repeat = 625;
  int r = sys_bpf(BPF_PROG_TEST_RUN, &a);
  uint32_t ret = a.test.retval;

  uint64_t want = init, mask = size == BPF_W ? 0xffffffffull : ~0ull;
  uint64_t opnd = (uint64_t)(int64_t)(int32_t)operand;
  for (int i = 0; i < 625 * 16; i++) {
    switch (op & ~BPF_FETCH) {
      case BPF_ADD: want = (want & ~mask) | ((want + opnd) & mask); break;
      case BPF_AND: want = (want & ~mask) | ((want & opnd) & mask); break;
      case BPF_OR: want = (want & ~mask) | ((want | opnd) & mask); break;
      case BPF_XOR: want = (want & ~mask) | ((want ^ opnd) & mask); break;
      case BPF_XCHG & ~BPF_FETCH: want = (want & ~mask) | (opnd & mask); break;
      case BPF_CMPXCHG & ~BPF_FETCH: want = (want & ~mask) | ((want + opnd) & mask); break;
    }
  }
  memset(&a, 0, sizeof(a));
  a.map_fd = map;
  a.key = (uintptr_t)&key;
  a.value = (uintptr_t)&v;
  sys_bpf(BPF_MAP_LOOKUP_ELEM, &a);
  unsigned jl = jited_len(fd);
  int ok = r == 0 && ret == 0 && v == want && (interp ? jl == 0 : jl > 0);
  if (ok) passes++; else fails++;
  printf("%s %s: 10000 ops, value 0x%llx, want 0x%llx, retval %u, JIT %u bytes%s\n",
         ok ? "PASS" : "FAIL", name, (unsigned long long)v, (unsigned long long)want, ret, jl,
         r ? " (test run failed)" : "");
  close(fd);
  close(map);
}

static int set_jit(const char* v) {
  FILE* f = fopen("/proc/sys/net/core/bpf_jit_enable", "w");
  if (!f) return -1;
  int r = fputs(v, f) < 0;
  return fclose(f) || r ? -1 : 0;
}

int main(int argc, char** argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  interp = argc > 1 && !strcmp(argv[1], "interp");
  if (interp && set_jit("0")) {
    printf("FAIL cannot set bpf_jit_enable to 0: %s\n", strerror(errno));
    return 1;
  }
  printf("mode: %s\n", interp ? "interpreter (bpf_jit_enable 0)" : "JIT");
  {
    union bpf_attr a;
    memset(&a, 0, sizeof(a));
    a.map_type = BPF_MAP_TYPE_ARRAY;
    a.key_size = 4;
    a.value_size = 48;
    a.max_entries = 1;
    map48 = sys_bpf(BPF_MAP_CREATE, &a);
    if (map48 < 0) {
      printf("FAIL array map for fixup_map_array_48b: %s\n", strerror(errno));
      return 1;
    }
  }
  int nt = sizeof(tests) / sizeof(tests[0]);
  for (int i = 0; i < nt; i++) {
    const struct bpf_test* t = &tests[i];
    char name[200], ev[256] = "";
    if (t->prog_type && t->prog_type != BPF_PROG_TYPE_SOCKET_FILTER) {
      skips++;
      printf("SKIP %s: needs program type %d%s%s (not in this kernel)\n", t->descr, t->prog_type,
             t->kfunc ? ", attach to " : "", t->kfunc ? t->kfunc : "");
      continue;
    }
    snprintf(name, sizeof(name), "%s", t->descr);
    report(name, run_case(t, 0, ev, sizeof(ev)), ev);
    char line[600];
    int f = run_unpriv(t, line, sizeof(line));
    snprintf(name, sizeof(name), "%s (unpriv)", t->descr);
    if (f) {
      fails++;
      printf("FAIL %s: %s\n", name, line);
    } else {
      passes++;
      printf("PASS %s: %s\n", name, line);
    }
  }
  precision_case();
  stress_case("loop atomic64_add", BPF_DW, BPF_ADD, 0, 3);
  stress_case("loop atomic_add (32-bit, wraps)", BPF_W, BPF_ADD, 0xfffffff0ull, 7);
  stress_case("loop atomic64_fetch_add", BPF_DW, BPF_ADD | BPF_FETCH, 5, 11);
  stress_case("loop atomic64_fetch_and", BPF_DW, BPF_AND | BPF_FETCH, ~0ull, 0x7ffffffe);
  stress_case("loop atomic64_fetch_or", BPF_DW, BPF_OR | BPF_FETCH, 0x100000000ull, 0x55);
  stress_case("loop atomic_fetch_xor (32-bit)", BPF_W, BPF_XOR | BPF_FETCH, 0x12345678, 0x0f0f0f0f);
  stress_case("loop atomic64_xchg", BPF_DW, BPF_XCHG, 1, 42);
  stress_case("loop atomic64_cmpxchg", BPF_DW, BPF_CMPXCHG, 0, 2);
  stress_case("loop atomic_cmpxchg (32-bit)", BPF_W, BPF_CMPXCHG, 0, 3);
  if (interp && set_jit("1")) printf("WARNING: bpf_jit_enable not restored to 1\n");
  printf("atomictest: %d passed, %d failed, %d skipped\n", passes, fails, skips);
  return fails ? 1 : 0;
}
