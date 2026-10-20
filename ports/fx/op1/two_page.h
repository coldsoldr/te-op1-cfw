// Two pages of four encoders in an effect position's values, for the new
// engines (plate, hall): values[0..3] are the shown page (the OP-1's knob
// handler writes them, within the host type's ranges); the hidden page and
// the page bit live in the engine's bits of values[4..7] (fx_host.h), every
// word within 0..32767:
//
//   values[4]  bit 0      page: 0 = page 1, 1 = page 2
//              bits 1-7   q3 bits 6-12
//   values[5]  bits 0-12  q0;  bits 13-14  q3 bits 4-5
//   values[6]  bits 0-12  q1;  bits 13-14  q3 bits 2-3
//   values[7]  bits 0-12  q2;  bits 13-14  q3 bits 0-1
//
// q0..q3: the hidden page's values (0..32767) >> 2, as offsets from the
// engine's defaults (mod 2^15), so all-zero bits are page 1 with page 2 at
// its defaults. A white click swaps the pages.

#ifndef OP1FX_OP1_TWO_PAGE_H_
#define OP1FX_OP1_TWO_PAGE_H_

#include <cstdint>

#include "fx/op1/fx_host.h"

namespace op1fx {

struct TwoPage {
  int32_t page;        // 0 or 1
  int32_t hidden[4];   // the hidden page's values, 0..32767
};

inline int32_t U16(const int16_t* v, int32_t k) { return static_cast<uint16_t>(v[k]); }

// defaults: 8 values, page-major.
inline TwoPage DecodeTwoPage(const int16_t* values, const int32_t* defaults) {
  TwoPage s;
  const int32_t w4 = U16(values, 4), w5 = U16(values, 5), w6 = U16(values, 6), w7 = U16(values, 7);
  s.page = w4 & 1;
  const int32_t q3 = ((w4 >> 1) & 0x7f) << 6 | ((w5 >> 13) & 3) << 4 | ((w6 >> 13) & 3) << 2 |
                     ((w7 >> 13) & 3);
  const int32_t q[4] = { w5 & 0x1fff, w6 & 0x1fff, w7 & 0x1fff, q3 };
  const int32_t base = (1 - s.page) * 4;
  for (int32_t k = 0; k < 4; ++k) s.hidden[k] = ((q[k] << 2) + defaults[base + k]) & 0x7fff;
  return s;
}

// Keeps the host's bits of values[4].
inline void EncodeTwoPage(const TwoPage& s, const int32_t* defaults, int16_t* values) {
  const int32_t base = (1 - s.page) * 4;
  int32_t q[4];
  for (int32_t k = 0; k < 4; ++k) q[k] = ((s.hidden[k] - defaults[base + k]) & 0x7fff) >> 2;
  values[4] = static_cast<int16_t>((U16(values, 4) & ~kEngineBits & 0x7fff) | (s.page & 1) |
                                   (q[3] >> 6) << 1);
  values[5] = static_cast<int16_t>(q[0] | ((q[3] >> 4) & 3) << 13);
  values[6] = static_cast<int16_t>(q[1] | ((q[3] >> 2) & 3) << 13);
  values[7] = static_cast<int16_t>(q[2] | (q[3] & 3) << 13);
}

// All eight values (page-major, 0..32767) from the encoders (as played) and
// the stored settings.
inline void TwoPageValues(const EncoderRange* ranges, const int16_t* encoders, const TwoPage& s,
                          int32_t out[8]) {
  const int32_t shown = s.page * 4, hidden = (1 - s.page) * 4;
  for (int32_t k = 0; k < 4; ++k) {
    out[shown + k] = EncoderToValue(ranges[k], encoders[k]);
    out[hidden + k] = s.hidden[k];
  }
}

// The white click: the shown page's values go to the hidden bits and the
// hidden page's to the encoders.
inline void SwapTwoPage(const EncoderRange* ranges, const int32_t* defaults, int16_t* values) {
  TwoPage s = DecodeTwoPage(values, defaults);
  int32_t shown[4];
  for (int32_t k = 0; k < 4; ++k) shown[k] = EncoderToValue(ranges[k], values[k]);
  for (int32_t k = 0; k < 4; ++k) {
    values[k] = ValueToEncoder(ranges[k], s.hidden[k]);
    s.hidden[k] = shown[k];
  }
  s.page ^= 1;
  EncodeTwoPage(s, defaults, values);
}

// A fresh start: page 1 at its defaults on the encoders, page 2 hidden at
// its defaults.
inline void EnterTwoPage(const EncoderRange* ranges, const int32_t* defaults, int16_t* values) {
  for (int32_t k = 0; k < 4; ++k) values[k] = ValueToEncoder(ranges[k], defaults[k]);
  TwoPage s;
  s.page = 0;
  for (int32_t k = 0; k < 4; ++k) s.hidden[k] = defaults[4 + k];
  EncodeTwoPage(s, defaults, values);
}

// A single-page engine's fresh start: its defaults (4 values) on the
// encoders; its bits of values[4..7] are left as the host cleared them.
inline void EnterOnePage(const EncoderRange* ranges, const int32_t* defaults, int16_t* values) {
  for (int32_t k = 0; k < 4; ++k) values[k] = ValueToEncoder(ranges[k], defaults[k]);
}

}  // namespace op1fx

#endif  // OP1FX_OP1_TWO_PAGE_H_
