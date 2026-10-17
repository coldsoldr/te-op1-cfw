/*
 * Blackfin grain overlap-add kernels. Same arithmetic as the C references
 * in grain_kernel.h (bit-exact; checked by tests/test_kernels.cc).
 *
 * void GrainLinearStereoAsm(GrainKernel* k, int32_t* out_l, int32_t* out_r, int32_t n);
 * void GrainHermiteStereoAsm(GrainKernel* k, int32_t* out_l, int32_t* out_r, int32_t n);
 * void GrainLinearMonoAsm(GrainKernel* k, int32_t* out, int32_t n);
 * void GrainHermiteMonoAsm(GrainKernel* k, int32_t* out, int32_t n);
 *
 * Registers in the loop: P5 -> k, P4 = base, P3 = r_offset (bytes), R7 =
 * phase, R6 = env (its high half is the Q15 gain), I2 / I3 -> outputs,
 * I0 / I1 -> left / right samples (half-register loads need I registers);
 * R0-R5, P0, P1 temporaries. The phase and env go back to *k at the end.
 *
 * Dual MACs take both halves from the same two registers, hence the gain
 * packing: gains_a = gl | gr, gains_b = cr | cl.
 */

#define K_BASE 0
#define K_ROFF 4
#define K_PHASE 8
#define K_INC 12
#define K_ENV 16
#define K_STEP 20
#define K_GA 24
#define K_GB 28

/* Saves, loads the state; P2 = n (from `count`); n <= 0 returns. */
#define PROLOGUE(count, done)                                              \
	LINK 0;                                                            \
	[--SP] = (R7:4, P5:3);                                             \
	[--SP] = LC0;                                                      \
	[--SP] = LT0;                                                      \
	[--SP] = LB0;                                                      \
	P5 = R0;                                                           \
	I2 = R1;                                                           \
	I3 = R2;                                                           \
	R3 = count;                                                        \
	CC = R3 <= 0;                                                      \
	IF CC JUMP done;                                                   \
	P2 = R3;                                                           \
	P4 = [P5 + K_BASE];                                                \
	P3 = [P5 + K_ROFF];                                                \
	R7 = [P5 + K_PHASE];                                               \
	R6 = [P5 + K_ENV];

#define EPILOGUE                                                           \
	[P5 + K_PHASE] = R7;                                               \
	[P5 + K_ENV] = R6;                                                 \
	LB0 = [SP++];                                                      \
	LT0 = [SP++];                                                      \
	LC0 = [SP++];                                                      \
	(R7:4, P5:3) = [SP++];                                             \
	UNLINK;                                                            \
	RTS;

/* I0 -> left samples at the read index (I1 -> right); R1 = t (Q15, upper
 * half 0). Clobbers R0, P0, P1. */
#define ADDRESS_STEREO                                                     \
	R0 = R7 >> 16;                                                     \
	P0 = R0;                                                           \
	P1 = P4 + (P0 << 1);                                               \
	I0 = P1;                                                           \
	P0 = P1 + P3;                                                      \
	I1 = P0;                                                           \
	R1 = R7 << 16;                                                     \
	R1 = R1 >> 17;

#define ADDRESS_MONO                                                       \
	R0 = R7 >> 16;                                                     \
	P0 = R0;                                                           \
	P1 = P4 + (P0 << 1);                                               \
	I0 = P1;                                                           \
	R1 = R7 << 16;                                                     \
	R1 = R1 >> 17;

/* Hermite weights of t (R1): R3 = wm1 (low) | w0 (high), R2 = w1 (low) |
 * w2 (high). Clobbers R0, R4, R5. */
#define HERMITE_WEIGHTS                                                    \
	R2.L = R1.L * R1.L (T);         /* t2 */                           \
	R2.H = R2.L * R1.L (T);         /* t3 */                           \
	R4 = R2.L (Z);                  /* t2 */                           \
	R5 = R2 >> 16;                  /* t3 */                           \
	R0 = R5 >> 1;                   /* t3 >> 1 */                      \
	R2 = R1 >> 1;                   /* t >> 1 */                       \
	R3 = R4 - R0;                                                      \
	R3 = R3 - R2;                   /* wm1 */                          \
	R2 = R2 + R4;                                                      \
	R2 = R2 + R4;                                                      \
	R2 = R2 - R5;                                                      \
	R2 = R2 - R0;                   /* w1 */                           \
	R0 = 0x7fff (Z);                                                   \
	R2 = MIN (R2, R0);                                                 \
	R5 = R5 - R4;                                                      \
	R5 >>>= 1;                      /* w2 */                           \
	R4 = R0 - R3;                                                      \
	R4 = R4 - R2;                                                      \
	R4 = R4 - R5;                                                      \
	R4 = MIN (R4, R0);              /* w0 */                           \
	R3 = PACK (R4.L, R3.L);                                                     \
	R2 = PACK (R5.L, R2.L);

/* Pan: R1 = l (low) | r (high) -> R4 = ml (low) | mr (high). */
#define PAN_STEREO                                                         \
	R5 = [P5 + K_GA];               /* gl | gr */                      \
	R0 = [P5 + K_GB];               /* cr | cl */                      \
	A1 = R1.H * R5.H, A0 = R1.L * R5.L;                                \
	R4.H = (A1 += R1.L * R0.H), R4.L = (A0 += R1.H * R0.L) (T);

/* out_l += (ml env) >>> 3, out_r += (mr env) >>> 3 for R4 = ml | mr; the
 * phase step; R0 = the env step (the loop's last instruction adds it). */
#define OUT_STEREO                                                         \
	R3 = R4.H * R6.H, R2 = R4.L * R6.H;                                \
	R2 >>>= 3;                                                         \
	R3 >>>= 3;                                                         \
	R0 = [I2];                                                         \
	R0 = R0 + R2;                                                      \
	[I2++] = R0;                                                       \
	R0 = [I3];                                                         \
	R0 = R0 + R3;                                                      \
	[I3++] = R0;                                                       \
	R0 = [P5 + K_INC];                                                 \
	R7 = R7 + R0;                                                      \
	R0 = [P5 + K_STEP];

/* out += (m env) >>> 3 for R4.L = m; as OUT_STEREO. */
#define OUT_MONO                                                           \
	R2 = R4.L * R6.H;                                                  \
	R2 >>>= 3;                                                         \
	R0 = [I2];                                                         \
	R0 = R0 + R2;                                                      \
	[I2++] = R0;                                                       \
	R0 = [P5 + K_INC];                                                 \
	R7 = R7 + R0;                                                      \
	R0 = [P5 + K_STEP];

	.text

	.align 4
	.global _GrainLinearStereoAsm
	.type _GrainLinearStereoAsm, STT_FUNC
_GrainLinearStereoAsm:
	PROLOGUE([FP + 20], .Lls_done)
	LSETUP (.Lls_start, .Lls_end) LC0 = P2;
.Lls_start:
	ADDRESS_STEREO
	R2 = ~R1;
	BITCLR (R2, 15);                /* u = 32767 - t */
	R3.L = W[I0++];                 /* x0 l */
	R3.H = W[I1++];                 /* x0 r */
	R4.L = W[I0];                   /* x1 l */
	R4.H = W[I1];                   /* x1 r */
	A1 = R3.H * R2.L, A0 = R3.L * R2.L;
	R1.H = (A1 += R4.H * R1.L), R1.L = (A0 += R4.L * R1.L) (T);
	PAN_STEREO
	OUT_STEREO
.Lls_end:
	R6 = R6 + R0;
.Lls_done:
	EPILOGUE
	.size _GrainLinearStereoAsm, .-_GrainLinearStereoAsm

	.align 4
	.global _GrainHermiteStereoAsm
	.type _GrainHermiteStereoAsm, STT_FUNC
_GrainHermiteStereoAsm:
	PROLOGUE([FP + 20], .Lhs_done)
	LSETUP (.Lhs_start, .Lhs_end) LC0 = P2;
.Lhs_start:
	ADDRESS_STEREO
	HERMITE_WEIGHTS
	R0.L = W[I0++];
	R0.H = W[I1++];                 /* xm1 */
	A1 = R0.H * R3.L, A0 = R0.L * R3.L;
	R0.L = W[I0++];
	R0.H = W[I1++];                 /* x0 */
	A1 += R0.H * R3.H, A0 += R0.L * R3.H;
	R0.L = W[I0++];
	R0.H = W[I1++];                 /* x1 */
	A1 += R0.H * R2.L, A0 += R0.L * R2.L;
	R0.L = W[I0];
	R0.H = W[I1];                   /* x2 */
	R1.H = (A1 += R0.H * R2.H), R1.L = (A0 += R0.L * R2.H) (T);
	PAN_STEREO
	OUT_STEREO
.Lhs_end:
	R6 = R6 + R0;
.Lhs_done:
	EPILOGUE
	.size _GrainHermiteStereoAsm, .-_GrainHermiteStereoAsm

	.align 4
	.global _GrainLinearMonoAsm
	.type _GrainLinearMonoAsm, STT_FUNC
_GrainLinearMonoAsm:
	PROLOGUE(R2, .Llm_done)
	LSETUP (.Llm_start, .Llm_end) LC0 = P2;
.Llm_start:
	ADDRESS_MONO
	R2 = ~R1;
	BITCLR (R2, 15);                /* u = 32767 - t */
	R3.L = W[I0++];                 /* x0 */
	R4.L = W[I0];                   /* x1 */
	A0 = R3.L * R2.L;
	R1.L = (A0 += R4.L * R1.L) (T);
	R5 = [P5 + K_GA];
	R4.L = R1.L * R5.L (T);
	OUT_MONO
.Llm_end:
	R6 = R6 + R0;
.Llm_done:
	EPILOGUE
	.size _GrainLinearMonoAsm, .-_GrainLinearMonoAsm

	.align 4
	.global _GrainHermiteMonoAsm
	.type _GrainHermiteMonoAsm, STT_FUNC
_GrainHermiteMonoAsm:
	PROLOGUE(R2, .Lhm_done)
	LSETUP (.Lhm_start, .Lhm_end) LC0 = P2;
.Lhm_start:
	ADDRESS_MONO
	HERMITE_WEIGHTS
	R0.L = W[I0++];                 /* xm1 */
	A0 = R0.L * R3.L;
	R0.L = W[I0++];                 /* x0 */
	A0 += R0.L * R3.H;
	R0.L = W[I0++];                 /* x1 */
	A0 += R0.L * R2.L;
	R0.L = W[I0];                   /* x2 */
	R1.L = (A0 += R0.L * R2.H) (T);
	R5 = [P5 + K_GA];
	R4.L = R1.L * R5.L (T);
	OUT_MONO
.Lhm_end:
	R6 = R6 + R0;
.Lhm_done:
	EPILOGUE
	.size _GrainHermiteMonoAsm, .-_GrainHermiteMonoAsm
