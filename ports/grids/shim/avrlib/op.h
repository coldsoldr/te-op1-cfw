// Host shim for avrlib/op.h: the C fallbacks of the AVR asm helpers that
// Grids' pattern generator uses (same results as the asm; pichenettes/avril
// op.h).

#ifndef SHIM_AVRLIB_OP_H_
#define SHIM_AVRLIB_OP_H_

#include <stdint.h>

namespace avrlib {

static inline uint8_t U8Mix(uint8_t a, uint8_t b, uint8_t balance) {
  return (a * (255 - balance) + b * balance) >> 8;
}

static inline uint8_t U8U8MulShift8(uint8_t a, uint8_t b) {
  return a * b >> 8;
}

static inline uint16_t U8U8Mul(uint8_t a, uint8_t b) {
  return a * b;
}

}  // namespace avrlib

#endif  // SHIM_AVRLIB_OP_H_
