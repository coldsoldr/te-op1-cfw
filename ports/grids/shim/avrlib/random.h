// Host shim for avrlib/random.h. Same generator as avrlib (pichenettes/avril
// random.h / random.cc): 16-bit Galois LFSR, x^16 + x^14 + x^13 + x^11,
// initial state 0x21. Seed() lets the harness run many sequences.

#ifndef SHIM_AVRLIB_RANDOM_H_
#define SHIM_AVRLIB_RANDOM_H_

#include <stdint.h>

namespace avrlib {

class Random {
 public:
  static inline void Update() {
    rng_state_ = (rng_state_ >> 1) ^ (-(rng_state_ & 1) & 0xb400);
  }
  static inline uint16_t state() { return rng_state_; }
  static inline void Seed(uint16_t seed) { rng_state_ = seed; }
  static inline uint8_t state_msb() {
    return static_cast<uint8_t>(rng_state_ >> 8);
  }
  static inline uint8_t GetByte() {
    Update();
    return state_msb();
  }
  static inline uint16_t GetWord() {
    Update();
    return state();
  }

 private:
  static uint16_t rng_state_;
};

}  // namespace avrlib

#endif  // SHIM_AVRLIB_RANDOM_H_
