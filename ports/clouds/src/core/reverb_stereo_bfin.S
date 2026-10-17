/*
 * Blackfin kernel for ReverbStereo's run: same arithmetic as ReverbRunC
 * (reverb_stereo.cc; bit-exact, checked by tests/test_kernels.cc).
 *
 * void ReverbRunAsm(ReverbKernel* k, int32_t* l, int32_t* r, int16_t* p,
 *                   int32_t n, int32_t alias_bytes);
 *
 * p points at the buffer's write pointer; each sample first moves it down
 * one. Every access is a fixed offset from it (reverb_layout.h); each
 * write also goes alias_bytes further on (0: the same place twice). n > 0.
 *
 * Registers: P2 = p, P3 = p + alias, P4 = alias, P5 -> k, I0 -> l, I1 ->
 * r; R4 = gain | amount, R5 = kap | -kap, R6 = krt | klp, R7 = lp_1 |
 * lp_2; A0 the accumulator, A1 the allpasses' output (apout); R0-R3, P1
 * temporaries.
 */

#include "src/core/reverb_layout.h"

/* Read(line TAIL, rs); WriteAllPass(line, ws): acc += prev rs; write the
 * acc's high half; acc = prev + written ws. */
#define ALLPASS(base, length, rs, ws)                                      \
	R1 = W[P2 + 2 * ((base) + (length) - 1)] (X);                      \
	R2.L = (A0 += R1.L * rs) (T);                                      \
	W[P2 + 2 * (base)] = R2;                                           \
	W[P3 + 2 * (base)] = R2;                                           \
	R1 <<= 16;                                                         \
	A0 = R1;                                                           \
	A0 += R2.L * ws;

/* R1.L = the interpolated tap at [P5 + off] bytes from p, weights at
 * [P5 + w]. */
#define TAP(off, w)                                                        \
	P1 = [P5 + off];                                                   \
	R3 = [P5 + w];                                                     \
	P1 = P1 + P2;                                                      \
	R1 = W[P1] (X);                                                    \
	R2 = W[P1 + 2] (X);                                                \
	A0 = R1.L * R3.L;                                                  \
	R1.L = (A0 += R2.L * R3.H) (T);

/* state = sat(state + (sat(acc - state) klp)); acc = state << 16. */
#define LP_LOW                                                             \
	R1.L = A0 (T);                                                     \
	R1.L = R1.L - R7.L (S);                                            \
	R1.L = R1.L * R6.H (T);                                            \
	R7.L = R7.L + R1.L (S);                                            \
	R1 = R7 << 16;                                                     \
	A0 = R1;

#define LP_HIGH                                                            \
	R1.L = A0 (T);                                                     \
	R1.L = R1.L - R7.H (S);                                            \
	R1.L = R1.L * R6.H (T);                                            \
	R7.H = R7.H + R1.L (S);                                            \
	R1 = R7;                                                           \
	R1.L = 0;                                                          \
	A0 = R1;

/* Write the acc to `base`; out = dry + floor((2 acc - dry) amount / 2^15)
 * at [I] (Q27), then I++. */
#define WRITE_OUT(base, I)                                                 \
	R1.L = A0 (T);                                                     \
	W[P2 + 2 * (base)] = R1;                                           \
	W[P3 + 2 * (base)] = R1;                                           \
	R1 = R1.L (X);                                                     \
	R1 <<= 13;                      /* wet, Q27 */                     \
	R0 = [I];                       /* dry */                          \
	R1 = R1 - R0 (NS);                                                 \
	A0 = R1.L * R4.H (FU);                                             \
	A0 = A0 >> 15;                                                     \
	A0 += R1.H * R4.H;                                                 \
	R2 = A0;                                                           \
	R0 = R0 + R2 (NS);                                                 \
	[I++] = R0;

	.text
	.align 4
	.global _ReverbRunAsm
	.type _ReverbRunAsm, STT_FUNC
_ReverbRunAsm:
	LINK 0;
	[--SP] = (R7:4, P5:3);
	[--SP] = LC0;
	[--SP] = LT0;
	[--SP] = LB0;

	P5 = R0;
	I0 = R1;
	I1 = R2;
	P2 = [FP + 20];                 /* p */
	P1 = [FP + 24];                 /* n */
	P4 = [FP + 28];                 /* alias, bytes */
	R4 = [P5 + RVK_GAIN_AMOUNT];
	R5 = [P5 + RVK_KAP];
	R6 = [P5 + RVK_KRT_KLP];
	R7 = [P5 + RVK_LP];

	LSETUP (.Lstart, .Lend) LC0 = P1;
.Lstart:
	P2 += -2;
	P3 = P2 + P4;

	/* Smear AP1 by LFO 1; Write(ap1, 100). */
	TAP(RVK_SMEAR_OFF, RVK_SMEAR_W)
	W[P2 + 2 * (RV_AP1 + RV_SMEAR_WRITE)] = R1;
	W[P3 + 2 * (RV_AP1 + RV_SMEAR_WRITE)] = R1;

	/* The input: (l + r) as Q15 (saturated), x gain. */
	R0 = [I0];
	R1 = [I1];
	R0 = R0 + R1 (NS);
	R0 = R0 << 4 (S);
	A0 = R0.H * R4.L;

	/* Diffuse through 4 allpasses. */
	ALLPASS(RV_AP1, RV_AP1_LEN, R5.L, R5.H)
	ALLPASS(RV_AP2, RV_AP2_LEN, R5.L, R5.H)
	ALLPASS(RV_AP3, RV_AP3_LEN, R5.L, R5.H)
	ALLPASS(RV_AP4, RV_AP4_LEN, R5.L, R5.H)
	A1 = A0;                        /* apout */

	/* First branch: del2's modulated tap, lp_1, dap1a, dap1b, del1. */
	TAP(RVK_D2_OFF, RVK_D2_W)
	A0 = A1;
	A0 += R1.L * R6.L;
	LP_LOW
	ALLPASS(RV_DAP1A, RV_DAP1A_LEN, R5.H, R5.L)
	ALLPASS(RV_DAP1B, RV_DAP1B_LEN, R5.L, R5.H)
	WRITE_OUT(RV_DEL1, I0)

	/* Second branch: del1's tail, lp_2, dap2a, dap2b, del2. */
	R1 = W[P2 + 2 * (RV_DEL1 + RV_DEL1_LEN - 1)] (X);
	A0 = A1;
	A0 += R1.L * R6.L;
	LP_HIGH
	ALLPASS(RV_DAP2A, RV_DAP2A_LEN, R5.L, R5.H)
	ALLPASS(RV_DAP2B, RV_DAP2B_LEN, R5.H, R5.L)
	WRITE_OUT(RV_DEL2, I1)
.Lend:
	NOP;

	[P5 + RVK_LP] = R7;
	LB0 = [SP++];
	LT0 = [SP++];
	LC0 = [SP++];
	(R7:4, P5:3) = [SP++];
	UNLINK;
	RTS;
	.size _ReverbRunAsm, .-_ReverbRunAsm
