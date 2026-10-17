// Marbles' random machinery in fixed point: the LCG, the random stream and
// the deja vu RandomSequence (upstream: marbles/random/random_generator.h,
// random_stream.h, random_sequence.h).
//
// Uniforms are Q32 integers holding exactly the value of upstream's float
// (float(word) / 2^32, emulated by WordToUnit), so with the same seed every
// draw, every loop value and every replayed value is bit-identical to
// upstream's (scaled by 2^32). Deterministic (shift register) values are
// kept as upstream stores them, 1.0f + value, i.e. rounded to 2^-23.
//
// Differences from upstream: no hardware RNG buffer (upstream's harness and
// the OP-1 have none: the LCG fallback is all that runs); pointers into the
// loop are indices; set_length avoids a division.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#ifndef OP1MARBLES_FX_RANDOM_FX_H_
#define OP1MARBLES_FX_RANDOM_FX_H_

#include <stdint.h>

#include "src/fx.h"

namespace op1marbles {
namespace fx {

class RandomGenerator {
 public:
  void Init(uint32_t seed) { state_ = seed; }
  inline uint32_t GetWord() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
  }
 private:
  uint32_t state_;
};

class RandomStream {
 public:
  void Init(RandomGenerator* generator) { generator_ = generator; }
  inline uint32_t GetWord() { return generator_->GetWord(); }
  // Upstream's GetFloat(), as Q32.
  inline uint32_t GetFloat() { return WordToUnit(GetWord()); }
 private:
  RandomGenerator* generator_;
};

const int kDejaVuBufferSize = 16;
const int kHistoryBufferSize = 16;

class RandomSequence {
 public:
  void Init(RandomStream* random_stream) {
    random_stream_ = random_stream;
    for (int i = 0; i < kDejaVuBufferSize; ++i) {
      loop_[i] = random_stream_->GetFloat();
    }
    loop_deterministic_ = 0;
    for (int i = 0; i < kHistoryBufferSize; ++i) history_[i] = 0;
    loop_write_head_ = 0;
    length_ = 8;
    step_ = 0;
    record_head_ = 0;
    replay_head_ = -1;
    replay_start_ = 0;
    deja_vu_ = 0;
    replay_hash_ = replay_shift_ = 0;
    redo_read_ = 0;
    redo_write_ = -1;
    redo_write_history_ = -1;
  }

  void Clone(const RandomSequence& s) {
    random_stream_ = s.random_stream_;
    for (int i = 0; i < kDejaVuBufferSize; ++i) loop_[i] = s.loop_[i];
    loop_deterministic_ = s.loop_deterministic_;
    for (int i = 0; i < kHistoryBufferSize; ++i) history_[i] = s.history_[i];
    loop_write_head_ = s.loop_write_head_;
    length_ = s.length_;
    step_ = s.step_;
    record_head_ = s.record_head_;
    replay_head_ = s.replay_head_;
    replay_start_ = s.replay_start_;
    replay_hash_ = s.replay_hash_;
    replay_shift_ = s.replay_shift_;
    deja_vu_ = s.deja_vu_;
    redo_read_ = s.redo_read_;
    redo_write_ = s.redo_write_;
    redo_write_history_ = s.redo_write_history_;
  }

  inline void Record() {
    replay_start_ = record_head_;
    replay_head_ = -1;
  }

  inline void ReplayPseudoRandom(uint32_t hash) {
    replay_head_ = replay_start_;
    replay_hash_ = hash;
    replay_shift_ = 0;
  }

  inline void ReplayShifted(uint32_t shift) {
    replay_head_ = replay_start_;
    replay_hash_ = 0;
    replay_shift_ = shift;
  }

  inline uint32_t GetReplayValue() const {
    uint32_t h = static_cast<uint32_t>(replay_head_ - 1 - static_cast<int32_t>(replay_shift_)
        + 2 * kHistoryBufferSize) % kHistoryBufferSize;
    if (!replay_hash_) {
      return history_[h];
    }
    // Upstream: word = float(history * 2^32); history values are floats
    // whose Q32 form is an integer, so the conversion is exact.
    uint32_t word = history_[h];
    word = (word ^ replay_hash_) * 1664525u + 1013904223u;
    return WordToUnit(word);
  }

  // value: Q32. See upstream for the purpose (register/ASR mode).
  inline uint32_t RewriteValue(uint32_t value) {
    if (replay_head_ >= 0) {
      return GetReplayValue();
    }
    if (redo_write_ >= 0) {
      StoreDeterministic(redo_write_, value);
    }
    uint32_t result = (loop_deterministic_ >> redo_read_) & 1
        ? loop_[redo_read_] : 0x80000000u;
    if (redo_write_history_ >= 0) {
      history_[redo_write_history_] = result;
    }
    return result;
  }

  inline uint32_t NextValue(bool deterministic, uint32_t value) {
    if (replay_head_ >= 0) {
      replay_head_ = (replay_head_ + 1) % kHistoryBufferSize;
      return GetReplayValue();
    }
    // p = (2 * deja_vu - 1)^2: exact in Q32 (d^2 with d in Q16), then
    // rounded to float precision as upstream's float product is.
    int32_t d = 2 * deja_vu_ - kQ16One;
    uint64_t p = RoundToFloat(static_cast<uint64_t>(static_cast<int64_t>(d) * d));
    const bool mutate = static_cast<uint64_t>(random_stream_->GetFloat()) < p;

    if (mutate && deja_vu_ <= kQ16One / 2) {
      redo_write_ = loop_write_head_;
      if (deterministic) {
        StoreDeterministic(loop_write_head_, value);
      } else {
        loop_[loop_write_head_] = random_stream_->GetFloat();
        loop_deterministic_ &= ~(1u << loop_write_head_);
      }
      loop_write_head_ = (loop_write_head_ + 1) % kDejaVuBufferSize;
      step_ = length_ - 1;
    } else {
      redo_write_ = -1;
      if (mutate) {
        // int(u * length) with the product rounded to float precision.
        uint64_t prod = RoundToFloat(Mul64(random_stream_->GetFloat(),
                                           static_cast<uint32_t>(length_)));
        step_ = static_cast<int>(prod >> 32);
      } else {
        step_ = step_ + 1;
        if (step_ >= length_) {
          step_ = 0;
        }
      }
    }
    int i = (loop_write_head_ + kDejaVuBufferSize - length_ + step_) % kDejaVuBufferSize;
    redo_read_ = i;
    uint32_t result = loop_[i];
    if (!((loop_deterministic_ >> i) & 1) && deterministic) {
      result = 0x80000000u;
    }
    redo_write_history_ = record_head_;
    history_[record_head_] = result;
    record_head_ = (record_head_ + 1) % kHistoryBufferSize;
    return result;
  }

  // size words, as upstream: seed from NextValue, then the LCG from the
  // seed's word, each output float(word) / 2^32.
  inline void NextVector(uint32_t* destination, int size) {
    uint32_t word = NextValue(false, 0);
    while (size--) {
      *destination++ = WordToUnit(word);
      word = word * 1664525u + 1013904223u;
    }
  }

  inline void set_deja_vu(int32_t deja_vu) { deja_vu_ = deja_vu; }

  inline void set_length(int length) {
    if (length < 1 || length > kDejaVuBufferSize) {
      return;
    }
    if (length != length_) {
      length_ = length;
      while (step_ >= length) step_ -= length;  // step_ % length, step_ < 16
    }
  }

  inline int32_t deja_vu() const { return deja_vu_; }
  inline int length() const { return length_; }
  void Reset() { step_ = length_ - 1; }

 private:
  // Upstream stores 1.0f + value: value rounded to a multiple of 2^-23.
  inline void StoreDeterministic(int index, uint32_t value) {
    uint64_t v = RoundToFloat((1ULL << 32) + value) - (1ULL << 32);
    loop_[index] = v > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(v);
    loop_deterministic_ |= 1u << index;
  }

  RandomStream* random_stream_;
  uint32_t loop_[kDejaVuBufferSize];
  uint32_t loop_deterministic_;  // bit i: loop_[i] came from the shift register
  uint32_t history_[kHistoryBufferSize];
  int loop_write_head_;
  int length_;
  int step_;
  int record_head_;
  int replay_head_;
  int replay_start_;
  uint32_t replay_hash_;
  uint32_t replay_shift_;
  int32_t deja_vu_;  // Q16
  int redo_read_;
  int redo_write_;
  int redo_write_history_;
};

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_RANDOM_FX_H_
