/*
 * Blackfin kernels for DiffuserFixed's run: same arithmetic as
 * DiffuserRunC (diffuser_fixed.cc; bit-exact, checked by
 * tests/test_kernels.cc).
 *
 * void DiffuserRunStereoAsm(const DiffuserKernel* k, int32_t* l, int32_t* r,
 *                           int16_t* p, int32_t n, int32_t alias_bytes);
 * void DiffuserRunMonoAsm(const DiffuserKernel* k, int32_t* l, int16_t* p,
 *                         int32_t n, int32_t alias_bytes);
 *
 * p points at the buffer's write pointer; each sample first moves it down
 * one; writes also go alias_bytes further on. n > 0.
 *
 * Registers: P2 = p, P3 = p + alias, P4 = alias, I0 -> l, I1 -> r; R4 =
 * kap | -kap, R5 = amount; A0 the accumulator; R0-R3 temporaries.
 */

#include "src/core/diffuser_layout.h"

#define ALLPASS(base, length)                                              \
	R1 = W[P2 + 2 * ((base) + (length) - 1)] (X);                      \
	R2.L = (A0 += R1.L * R4.L) (T);                                    \
	W[P2 + 2 * (base)] = R2;                                           \
	W[P3 + 2 * (base)] = R2;                                           \
	R1 <<= 16;                                                         \
	A0 = R1;                                                           \
	A0 += R2.L * R4.H;

/* One side on [I]: the input as Q14 into the accumulator's high half,
 * four allpasses, out = dry + floor((wet - dry) amount / 2^15), I++. */
#define SIDE(I, b1, l1, b2, l2, b3, l3, b4, l4)                            \
	R0 = [I];                                                          \
	R1 = R0 << 3 (S);                                                  \
	R1.L = 0;                                                          \
	A0 = R1;                                                           \
	ALLPASS(b1, l1)                                                    \
	ALLPASS(b2, l2)                                                    \
	ALLPASS(b3, l3)                                                    \
	ALLPASS(b4, l4)                                                    \
	R1.L = A0 (T);                                                     \
	R1 = R1.L (X);                                                     \
	R1 <<= 13;                      /* wet, Q27 */                     \
	R1 = R1 - R0 (NS);                                                 \
	A0 = R1.L * R5.L (FU);                                             \
	A0 = A0 >> 15;                                                     \
	A0 += R1.H * R5.L;                                                 \
	R2 = A0;                                                           \
	R0 = R0 + R2 (NS);                                                 \
	[I++] = R0;

#define PROLOGUE                                                           \
	LINK 0;                                                            \
	[--SP] = (R7:4, P5:3);                                             \
	[--SP] = LC0;                                                      \
	[--SP] = LT0;                                                      \
	[--SP] = LB0;                                                      \
	P5 = R0;                                                           \
	R4 = [P5];                                                         \
	R5 = [P5 + 4];

#define EPILOGUE                                                           \
	LB0 = [SP++];                                                      \
	LT0 = [SP++];                                                      \
	LC0 = [SP++];                                                      \
	(R7:4, P5:3) = [SP++];                                             \
	UNLINK;                                                            \
	RTS;

	.text
	.align 4
	.global _DiffuserRunStereoAsm
	.type _DiffuserRunStereoAsm, STT_FUNC
_DiffuserRunStereoAsm:
	PROLOGUE
	I0 = R1;
	I1 = R2;
	P2 = [FP + 20];                 /* p */
	P1 = [FP + 24];                 /* n */
	P4 = [FP + 28];                 /* alias, bytes */
	LSETUP (.Ls_start, .Ls_end) LC0 = P1;
.Ls_start:
	P2 += -2;
	P3 = P2 + P4;
	SIDE(I0, DF_L1, DF_L1_LEN, DF_L2, DF_L2_LEN, DF_L3, DF_L3_LEN, DF_L4, DF_L4_LEN)
	SIDE(I1, DF_R1, DF_R1_LEN, DF_R2, DF_R2_LEN, DF_R3, DF_R3_LEN, DF_R4, DF_R4_LEN)
.Ls_end:
	NOP;
	EPILOGUE
	.size _DiffuserRunStereoAsm, .-_DiffuserRunStereoAsm

	.align 4
	.global _DiffuserRunMonoAsm
	.type _DiffuserRunMonoAsm, STT_FUNC
_DiffuserRunMonoAsm:
	PROLOGUE
	I0 = R1;
	P2 = R2;                        /* p */
	P1 = [FP + 20];                 /* n */
	P4 = [FP + 24];                 /* alias, bytes */
	LSETUP (.Lm_start, .Lm_end) LC0 = P1;
.Lm_start:
	P2 += -2;
	P3 = P2 + P4;
	SIDE(I0, DF_L1, DF_L1_LEN, DF_L2, DF_L2_LEN, DF_L3, DF_L3_LEN, DF_L4, DF_L4_LEN)
.Lm_end:
	NOP;
	EPILOGUE
	.size _DiffuserRunMonoAsm, .-_DiffuserRunMonoAsm
