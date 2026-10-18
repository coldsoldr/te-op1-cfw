/*
 * Straight-line code of known sizes, for the CPU probe's instruction
 * cache test (op1/cpu_probe.cc): 16-bit NOPs, so a run costs one cycle
 * per instruction once the code is in the cache; a cold run adds the
 * fetch misses (one per 32-byte line).
 *
 * void Sled8k();  void Sled12k();  void Sled32k();
 */
	.text

	.align 4
	.global _Sled8k
	.type _Sled8k, STT_FUNC
_Sled8k:
	.rept 4096
	NOP;
	.endr
	RTS;
	.size _Sled8k, .-_Sled8k

	.align 4
	.global _Sled12k
	.type _Sled12k, STT_FUNC
_Sled12k:
	.rept 6144
	NOP;
	.endr
	RTS;
	.size _Sled12k, .-_Sled12k

	.align 4
	.global _Sled32k
	.type _Sled32k, STT_FUNC
_Sled32k:
	.rept 16384
	NOP;
	.endr
	RTS;
	.size _Sled32k, .-_Sled32k
