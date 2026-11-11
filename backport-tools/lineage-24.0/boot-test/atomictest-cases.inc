/* verifier/atomic_*.c from linux v6.1 tools/testing/selftests/bpf (and, bounds, cmpxchg,
 * fetch, fetch_add, invalid, or, xchg, xor), unchanged. */
{
	"BPF_ATOMIC_AND without fetch",
	.insns = {
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* atomic_and(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_AND, BPF_REG_10, BPF_REG_1, -8),
		/* if (val != 0x010) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0x010, 2),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* r1 should not be clobbered, no BPF_FETCH flag */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x011, 1),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC_AND with fetch",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 123),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* old = atomic_fetch_and(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_AND | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 0x110) exit(3); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x010) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x010, 2),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (for fear of x86 JIT bug) */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 123, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC_AND with fetch 32bit",
	.insns = {
		/* r0 = (s64) -1 */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 1),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0x110),
		/* old = atomic_fetch_and(&val, 0x011); */
		BPF_MOV32_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_W, BPF_AND | BPF_FETCH, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 0x110) exit(3); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x010) exit(2); */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x010, 2),
		BPF_MOV32_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (for fear of x86 JIT bug)
		 * It should be -1 so add 1 to get exit code.
		 */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC_AND with fetch - r0 as source reg",
	.insns = {
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* old = atomic_fetch_and(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_0, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_AND | BPF_FETCH, BPF_REG_10, BPF_REG_0, -8),
		/* if (old != 0x110) exit(3); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0x110, 2),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x010) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x010, 2),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC bounds propagation, mem->reg",
	.insns = {
		/* a = 0; */
		/*
		 * Note this is implemented with two separate instructions,
		 * where you might think one would suffice:
		 *
		 * BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
		 *
		 * This is because BPF_ST_MEM doesn't seem to set the stack slot
		 * type to 0 when storing an immediate.
		 */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
		/* b = atomic_fetch_add(&a, 1); */
		BPF_MOV64_IMM(BPF_REG_1, 1),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* Verifier should be able to tell that this infinite loop isn't reachable. */
		/* if (b) while (true) continue; */
		BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, -1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "back-edge",
},
{
	"atomic compare-and-exchange smoketest - 64bit",
	.insns = {
		/* val = 3; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		/* old = atomic_cmpxchg(&val, 2, 4); */
		BPF_MOV64_IMM(BPF_REG_1, 4),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 3) exit(2); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* if (val != 3) exit(3); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* old = atomic_cmpxchg(&val, 3, 4); */
		BPF_MOV64_IMM(BPF_REG_1, 4),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 3) exit(4); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 4),
		BPF_EXIT_INSN(),
		/* if (val != 4) exit(5); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 4, 2),
		BPF_MOV64_IMM(BPF_REG_0, 5),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"atomic compare-and-exchange smoketest - 32bit",
	.insns = {
		/* val = 3; */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 3),
		/* old = atomic_cmpxchg(&val, 2, 4); */
		BPF_MOV32_IMM(BPF_REG_1, 4),
		BPF_MOV32_IMM(BPF_REG_0, 2),
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 3) exit(2); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV32_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* if (val != 3) exit(3); */
		BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* old = atomic_cmpxchg(&val, 3, 4); */
		BPF_MOV32_IMM(BPF_REG_1, 4),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 3) exit(4); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 3, 2),
		BPF_MOV32_IMM(BPF_REG_0, 4),
		BPF_EXIT_INSN(),
		/* if (val != 4) exit(5); */
		BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 4, 2),
		BPF_MOV32_IMM(BPF_REG_0, 5),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"Can't use cmpxchg on uninit src reg",
	.insns = {
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_2, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "!read_ok",
},
{
	"Can't use cmpxchg on uninit memory",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_MOV64_IMM(BPF_REG_2, 4),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_2, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "invalid read from stack",
},
{
	"BPF_W cmpxchg should zero top 32 bits",
	.insns = {
		/* r0 = U64_MAX; */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 1),
		/* u64 val = r0; */
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
		/* r0 = (u32)atomic_cmpxchg((u32 *)&val, r0, 1); */
		BPF_MOV32_IMM(BPF_REG_1, 1),
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8),
		/* r1 = 0x00000000FFFFFFFFull; */
		BPF_MOV64_IMM(BPF_REG_1, 1),
		BPF_ALU64_IMM(BPF_LSH, BPF_REG_1, 32),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 1),
		/* if (r0 != r1) exit(1); */
		BPF_JMP_REG(BPF_JEQ, BPF_REG_0, BPF_REG_1, 2),
		BPF_MOV32_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"Dest pointer in r0 - fail",
	.insns = {
		/* val = 0; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
		/* r0 = &val */
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_10),
		/* r0 = atomic_cmpxchg(&val, r0, 1); */
		BPF_MOV64_IMM(BPF_REG_1, 1),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8),
		/* if (r0 != 0) exit(1); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R0 leaks addr into mem",
},
{
	"Dest pointer in r0 - succeed",
	.insns = {
		/* r0 = &val */
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_10),
		/* val = r0; */
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
		/* r0 = atomic_cmpxchg(&val, r0, 0); */
		BPF_MOV64_IMM(BPF_REG_1, 0),
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8),
		/* r1 = *r0 */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, -8),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R0 leaks addr into mem",
},
{
	"Dest pointer in r0 - succeed, check 2",
	.insns = {
		/* r0 = &val */
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_10),
		/* val = r0; */
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
		/* r5 = &val */
		BPF_MOV64_REG(BPF_REG_5, BPF_REG_10),
		/* r0 = atomic_cmpxchg(&val, r0, r5); */
		BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_10, BPF_REG_5, -8),
		/* r1 = *r0 */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, -8),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R0 leaks addr into mem",
},
{
	"Dest pointer in r0 - succeed, check 3",
	.insns = {
		/* r0 = &val */
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_10),
		/* val = r0; */
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
		/* r5 = &val */
		BPF_MOV64_REG(BPF_REG_5, BPF_REG_10),
		/* r0 = atomic_cmpxchg(&val, r0, r5); */
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_5, -8),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "invalid size of register fill",
	.errstr_unpriv = "R0 leaks addr into mem",
},
{
	"Dest pointer in r0 - succeed, check 4",
	.insns = {
		/* r0 = &val */
		BPF_MOV32_REG(BPF_REG_0, BPF_REG_10),
		/* val = r0; */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -8),
		/* r5 = &val */
		BPF_MOV32_REG(BPF_REG_5, BPF_REG_10),
		/* r0 = atomic_cmpxchg(&val, r0, r5); */
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_5, -8),
		/* r1 = *r10 */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -8),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "R10 partial copy of pointer",
},
{
	"Dest pointer in r0 - succeed, check 5",
	.insns = {
		/* r0 = &val */
		BPF_MOV32_REG(BPF_REG_0, BPF_REG_10),
		/* val = r0; */
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -8),
		/* r5 = &val */
		BPF_MOV32_REG(BPF_REG_5, BPF_REG_10),
		/* r0 = atomic_cmpxchg(&val, r0, r5); */
		BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_5, -8),
		/* r1 = *r0 */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_0, -8),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr = "R0 invalid mem access",
	.errstr_unpriv = "R10 partial copy of pointer",
},
{
	"atomic dw/fetch and address leakage of (map ptr & -1) via stack slot",
	.insns = {
		BPF_LD_IMM64(BPF_REG_1, -1),
		BPF_LD_MAP_FD(BPF_REG_8, 0),
		BPF_LD_MAP_FD(BPF_REG_9, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_9, 0),
		BPF_ATOMIC_OP(BPF_DW, BPF_AND | BPF_FETCH, BPF_REG_2, BPF_REG_1, 0),
		BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_2, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_8),
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_STX_MEM(BPF_DW, BPF_REG_0, BPF_REG_9, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.fixup_map_array_48b = { 2, 4 },
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "leaking pointer from stack off -8",
},
{
	"atomic dw/fetch and address leakage of (map ptr & -1) via returned value",
	.insns = {
		BPF_LD_IMM64(BPF_REG_1, -1),
		BPF_LD_MAP_FD(BPF_REG_8, 0),
		BPF_LD_MAP_FD(BPF_REG_9, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_9, 0),
		BPF_ATOMIC_OP(BPF_DW, BPF_AND | BPF_FETCH, BPF_REG_2, BPF_REG_1, 0),
		BPF_MOV64_REG(BPF_REG_9, BPF_REG_1),
		BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_8),
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_STX_MEM(BPF_DW, BPF_REG_0, BPF_REG_9, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.fixup_map_array_48b = { 2, 4 },
	.result = ACCEPT,
	.result_unpriv = REJECT,
	.errstr_unpriv = "leaking pointer from stack off -8",
},
{
	"atomic w/fetch and address leakage of (map ptr & -1) via stack slot",
	.insns = {
		BPF_LD_IMM64(BPF_REG_1, -1),
		BPF_LD_MAP_FD(BPF_REG_8, 0),
		BPF_LD_MAP_FD(BPF_REG_9, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_9, 0),
		BPF_ATOMIC_OP(BPF_W, BPF_AND | BPF_FETCH, BPF_REG_2, BPF_REG_1, 0),
		BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_2, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_8),
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_STX_MEM(BPF_DW, BPF_REG_0, BPF_REG_9, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.fixup_map_array_48b = { 2, 4 },
	.result = REJECT,
	.errstr = "invalid size of register fill",
},
{
	"atomic w/fetch and address leakage of (map ptr & -1) via returned value",
	.insns = {
		BPF_LD_IMM64(BPF_REG_1, -1),
		BPF_LD_MAP_FD(BPF_REG_8, 0),
		BPF_LD_MAP_FD(BPF_REG_9, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_9, 0),
		BPF_ATOMIC_OP(BPF_W, BPF_AND | BPF_FETCH, BPF_REG_2, BPF_REG_1, 0),
		BPF_MOV64_REG(BPF_REG_9, BPF_REG_1),
		BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_8),
		BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_STX_MEM(BPF_DW, BPF_REG_0, BPF_REG_9, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.fixup_map_array_48b = { 2, 4 },
	.result = REJECT,
	.errstr = "invalid size of register fill",
},
#define __ATOMIC_FETCH_OP_TEST(src_reg, dst_reg, operand1, op, operand2, expect) \
	{								\
		"atomic fetch " #op ", src=" #dst_reg " dst=" #dst_reg,	\
		.insns = {						\
			/* u64 val = operan1; */			\
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, operand1),	\
			/* u64 old = atomic_fetch_add(&val, operand2); */ \
			BPF_MOV64_REG(dst_reg, BPF_REG_10),		\
			BPF_MOV64_IMM(src_reg, operand2),		\
			BPF_ATOMIC_OP(BPF_DW, op,			\
				      dst_reg, src_reg, -8),		\
			/* if (old != operand1) exit(1); */		\
			BPF_JMP_IMM(BPF_JEQ, src_reg, operand1, 2),	\
			BPF_MOV64_IMM(BPF_REG_0, 1),			\
			BPF_EXIT_INSN(),				\
			/* if (val != result) exit (2); */		\
			BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),	\
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, expect, 2),	\
			BPF_MOV64_IMM(BPF_REG_0, 2),			\
			BPF_EXIT_INSN(),				\
			/* exit(0); */					\
			BPF_MOV64_IMM(BPF_REG_0, 0),			\
			BPF_EXIT_INSN(),				\
		},							\
		.result = ACCEPT,					\
	}
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_2, 1, BPF_ADD | BPF_FETCH, 2, 3),
__ATOMIC_FETCH_OP_TEST(BPF_REG_0, BPF_REG_1, 1, BPF_ADD | BPF_FETCH, 2, 3),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_0, 1, BPF_ADD | BPF_FETCH, 2, 3),
__ATOMIC_FETCH_OP_TEST(BPF_REG_2, BPF_REG_3, 1, BPF_ADD | BPF_FETCH, 2, 3),
__ATOMIC_FETCH_OP_TEST(BPF_REG_4, BPF_REG_5, 1, BPF_ADD | BPF_FETCH, 2, 3),
__ATOMIC_FETCH_OP_TEST(BPF_REG_9, BPF_REG_8, 1, BPF_ADD | BPF_FETCH, 2, 3),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_2, 0x010, BPF_AND | BPF_FETCH, 0x011, 0x010),
__ATOMIC_FETCH_OP_TEST(BPF_REG_0, BPF_REG_1, 0x010, BPF_AND | BPF_FETCH, 0x011, 0x010),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_0, 0x010, BPF_AND | BPF_FETCH, 0x011, 0x010),
__ATOMIC_FETCH_OP_TEST(BPF_REG_2, BPF_REG_3, 0x010, BPF_AND | BPF_FETCH, 0x011, 0x010),
__ATOMIC_FETCH_OP_TEST(BPF_REG_4, BPF_REG_5, 0x010, BPF_AND | BPF_FETCH, 0x011, 0x010),
__ATOMIC_FETCH_OP_TEST(BPF_REG_9, BPF_REG_8, 0x010, BPF_AND | BPF_FETCH, 0x011, 0x010),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_2, 0x010, BPF_OR | BPF_FETCH, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_0, BPF_REG_1, 0x010, BPF_OR | BPF_FETCH, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_0, 0x010, BPF_OR | BPF_FETCH, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_2, BPF_REG_3, 0x010, BPF_OR | BPF_FETCH, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_4, BPF_REG_5, 0x010, BPF_OR | BPF_FETCH, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_9, BPF_REG_8, 0x010, BPF_OR | BPF_FETCH, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_2, 0x010, BPF_XOR | BPF_FETCH, 0x011, 0x001),
__ATOMIC_FETCH_OP_TEST(BPF_REG_0, BPF_REG_1, 0x010, BPF_XOR | BPF_FETCH, 0x011, 0x001),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_0, 0x010, BPF_XOR | BPF_FETCH, 0x011, 0x001),
__ATOMIC_FETCH_OP_TEST(BPF_REG_2, BPF_REG_3, 0x010, BPF_XOR | BPF_FETCH, 0x011, 0x001),
__ATOMIC_FETCH_OP_TEST(BPF_REG_4, BPF_REG_5, 0x010, BPF_XOR | BPF_FETCH, 0x011, 0x001),
__ATOMIC_FETCH_OP_TEST(BPF_REG_9, BPF_REG_8, 0x010, BPF_XOR | BPF_FETCH, 0x011, 0x001),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_2, 0x010, BPF_XCHG, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_0, BPF_REG_1, 0x010, BPF_XCHG, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_1, BPF_REG_0, 0x010, BPF_XCHG, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_2, BPF_REG_3, 0x010, BPF_XCHG, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_4, BPF_REG_5, 0x010, BPF_XCHG, 0x011, 0x011),
__ATOMIC_FETCH_OP_TEST(BPF_REG_9, BPF_REG_8, 0x010, BPF_XCHG, 0x011, 0x011),
#undef __ATOMIC_FETCH_OP_TEST
{
	"BPF_ATOMIC_FETCH_ADD smoketest - 64bit",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		/* Write 3 to stack */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		/* Put a 1 in R1, add it to the 3 on the stack, and load the value back into R1 */
		BPF_MOV64_IMM(BPF_REG_1, 1),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* Check the value we loaded back was 3 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* Load value from stack */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),
		/* Check value loaded from stack was 4 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 4, 1),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC_FETCH_ADD smoketest - 32bit",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		/* Write 3 to stack */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 3),
		/* Put a 1 in R1, add it to the 3 on the stack, and load the value back into R1 */
		BPF_MOV32_IMM(BPF_REG_1, 1),
		BPF_ATOMIC_OP(BPF_W, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -4),
		/* Check the value we loaded back was 3 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* Load value from stack */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -4),
		/* Check value loaded from stack was 4 */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 4, 1),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"Can't use ATM_FETCH_ADD on frame pointer",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_10, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	.errstr_unpriv = "R10 leaks addr into mem",
	.errstr = "frame pointer is read only",
},
{
	"Can't use ATM_FETCH_ADD on uninit src reg",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_2, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	/* It happens that the address leak check is first, but it would also be
	 * complain about the fact that we're trying to modify R10.
	 */
	.errstr = "!read_ok",
},
{
	"Can't use ATM_FETCH_ADD on uninit dst reg",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_2, BPF_REG_0, -8),
		BPF_EXIT_INSN(),
	},
	.result = REJECT,
	/* It happens that the address leak check is first, but it would also be
	 * complain about the fact that we're trying to modify R10.
	 */
	.errstr = "!read_ok",
},
{
	"Can't use ATM_FETCH_ADD on kernel memory",
	.insns = {
		/* This is an fentry prog, context is array of the args of the
		 * kernel function being called. Load first arg into R2.
		 */
		BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 0),
		/* First arg of bpf_fentry_test7 is a pointer to a struct.
		 * Attempt to modify that struct. Verifier shouldn't let us
		 * because it's kernel memory.
		 */
		BPF_MOV64_IMM(BPF_REG_3, 1),
		BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_2, BPF_REG_3, 0),
		/* Done */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACING,
	.expected_attach_type = BPF_TRACE_FENTRY,
	.kfunc = "bpf_fentry_test7",
	.result = REJECT,
	.errstr = "only read is supported",
},
#define __INVALID_ATOMIC_ACCESS_TEST(op)				\
	{								\
		"atomic " #op " access through non-pointer ",		\
		.insns = {						\
			BPF_MOV64_IMM(BPF_REG_0, 1),			\
			BPF_MOV64_IMM(BPF_REG_1, 0),			\
			BPF_ATOMIC_OP(BPF_DW, op, BPF_REG_1, BPF_REG_0, -8), \
			BPF_MOV64_IMM(BPF_REG_0, 0),			\
			BPF_EXIT_INSN(),				\
		},							\
		.result = REJECT,					\
		.errstr = "R1 invalid mem access 'scalar'"		\
	}
__INVALID_ATOMIC_ACCESS_TEST(BPF_ADD),
__INVALID_ATOMIC_ACCESS_TEST(BPF_ADD | BPF_FETCH),
__INVALID_ATOMIC_ACCESS_TEST(BPF_ADD),
__INVALID_ATOMIC_ACCESS_TEST(BPF_ADD | BPF_FETCH),
__INVALID_ATOMIC_ACCESS_TEST(BPF_AND),
__INVALID_ATOMIC_ACCESS_TEST(BPF_AND | BPF_FETCH),
__INVALID_ATOMIC_ACCESS_TEST(BPF_OR),
__INVALID_ATOMIC_ACCESS_TEST(BPF_OR | BPF_FETCH),
__INVALID_ATOMIC_ACCESS_TEST(BPF_XOR),
__INVALID_ATOMIC_ACCESS_TEST(BPF_XOR | BPF_FETCH),
__INVALID_ATOMIC_ACCESS_TEST(BPF_XCHG),
__INVALID_ATOMIC_ACCESS_TEST(BPF_CMPXCHG),
{
	"BPF_ATOMIC OR without fetch",
	.insns = {
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* atomic_or(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_OR, BPF_REG_10, BPF_REG_1, -8),
		/* if (val != 0x111) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0x111, 2),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* r1 should not be clobbered, no BPF_FETCH flag */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x011, 1),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC OR with fetch",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 123),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* old = atomic_fetch_or(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 0x110) exit(3); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x111) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x111, 2),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (for fear of x86 JIT bug) */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 123, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC OR with fetch 32bit",
	.insns = {
		/* r0 = (s64) -1 */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 1),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0x110),
		/* old = atomic_fetch_or(&val, 0x011); */
		BPF_MOV32_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_W, BPF_OR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 0x110) exit(3); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x111) exit(2); */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x111, 2),
		BPF_MOV32_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (for fear of x86 JIT bug)
		 * It should be -1 so add 1 to get exit code.
		 */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_W atomic_fetch_or should zero top 32 bits",
	.insns = {
		/* r1 = U64_MAX; */
		BPF_MOV64_IMM(BPF_REG_1, 0),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 1),
		/* u64 val = r1; */
		BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8),
		/* r1 = (u32)atomic_fetch_or((u32 *)&val, 2); */
		BPF_MOV32_IMM(BPF_REG_1, 2),
		BPF_ATOMIC_OP(BPF_W, BPF_OR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* r2 = 0x00000000FFFFFFFF; */
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 32),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_2, 1),
		/* if (r2 != r1) exit(1); */
		BPF_JMP_REG(BPF_JEQ, BPF_REG_2, BPF_REG_1, 2),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"atomic exchange smoketest - 64bit",
	.insns = {
		/* val = 3; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 3),
		/* old = atomic_xchg(&val, 4); */
		BPF_MOV64_IMM(BPF_REG_1, 4),
		BPF_ATOMIC_OP(BPF_DW, BPF_XCHG, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 3) exit(1); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 3, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* if (val != 4) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 4, 2),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"atomic exchange smoketest - 32bit",
	.insns = {
		/* val = 3; */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 3),
		/* old = atomic_xchg(&val, 4); */
		BPF_MOV32_IMM(BPF_REG_1, 4),
		BPF_ATOMIC_OP(BPF_W, BPF_XCHG, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 3) exit(1); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 3, 2),
		BPF_MOV32_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* if (val != 4) exit(2); */
		BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_0, 4, 2),
		BPF_MOV32_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV32_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC XOR without fetch",
	.insns = {
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* atomic_xor(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_XOR, BPF_REG_10, BPF_REG_1, -8),
		/* if (val != 0x101) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0x101, 2),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* r1 should not be clobbered, no BPF_FETCH flag */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x011, 1),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC XOR with fetch",
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 123),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x110),
		/* old = atomic_fetch_xor(&val, 0x011); */
		BPF_MOV64_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_DW, BPF_XOR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8),
		/* if (old != 0x110) exit(3); */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x101) exit(2); */
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x101, 2),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (fxor fear of x86 JIT bug) */
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 123, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* exit(0); */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"BPF_ATOMIC XOR with fetch 32bit",
	.insns = {
		/* r0 = (s64) -1 */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 1),
		/* val = 0x110; */
		BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0x110),
		/* old = atomic_fetch_xor(&val, 0x011); */
		BPF_MOV32_IMM(BPF_REG_1, 0x011),
		BPF_ATOMIC_OP(BPF_W, BPF_XOR | BPF_FETCH, BPF_REG_10, BPF_REG_1, -4),
		/* if (old != 0x110) exit(3); */
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x110, 2),
		BPF_MOV32_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* if (val != 0x101) exit(2); */
		BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -4),
		BPF_JMP32_IMM(BPF_JEQ, BPF_REG_1, 0x101, 2),
		BPF_MOV32_IMM(BPF_REG_1, 2),
		BPF_EXIT_INSN(),
		/* Check R0 wasn't clobbered (fxor fear of x86 JIT bug)
		 * It should be -1 so add 1 to get exit code.
		 */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
