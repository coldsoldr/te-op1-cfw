/* Clouds' diffuser delay layout (upstream's lengths at 32 kHz scaled to the
 * build's rate; bases packed with one sample between lines, as FxEngine),
 * shared by diffuser_fixed.h and diffuser_bfin.S. diffuser_fixed.h checks
 * it against Scaled(). Left network: DF_L1..L4; right: DF_R1..R4. */

#ifndef OP1CLOUDS_CORE_DIFFUSER_LAYOUT_H_
#define OP1CLOUDS_CORE_DIFFUSER_LAYOUT_H_

#if OP1CLOUDS_SAMPLE_RATE == 32000
#define DF_SIZE 2048
#define DF_L1 0
#define DF_L1_LEN 126
#define DF_L2 127
#define DF_L2_LEN 180
#define DF_L3 308
#define DF_L3_LEN 269
#define DF_L4 578
#define DF_L4_LEN 444
#define DF_R1 1023
#define DF_R1_LEN 151
#define DF_R2 1175
#define DF_R2_LEN 205
#define DF_R3 1381
#define DF_R3_LEN 245
#define DF_R4 1627
#define DF_R4_LEN 405
#elif OP1CLOUDS_SAMPLE_RATE == 22050
#define DF_SIZE 2048
#define DF_L1 0
#define DF_L1_LEN 87
#define DF_L2 88
#define DF_L2_LEN 124
#define DF_L3 213
#define DF_L3_LEN 185
#define DF_L4 399
#define DF_L4_LEN 306
#define DF_R1 706
#define DF_R1_LEN 104
#define DF_R2 811
#define DF_R2_LEN 141
#define DF_R3 953
#define DF_R3_LEN 169
#define DF_R4 1123
#define DF_R4_LEN 279
#else
#define DF_SIZE 4096
#define DF_L1 0
#define DF_L1_LEN 174
#define DF_L2 175
#define DF_L2_LEN 248
#define DF_L3 424
#define DF_L3_LEN 371
#define DF_L4 796
#define DF_L4_LEN 612
#define DF_R1 1409
#define DF_R1_LEN 208
#define DF_R2 1618
#define DF_R2_LEN 283
#define DF_R3 1902
#define DF_R3_LEN 338
#define DF_R4 2241
#define DF_R4_LEN 558
#endif

#define DF_MAX_REACH (DF_R4 + DF_R4_LEN)
#define DF_BUFFER_WORDS (DF_SIZE + DF_MAX_REACH + DF_R4 + 2)

#endif  /* OP1CLOUDS_CORE_DIFFUSER_LAYOUT_H_ */
