// Clouds' diffuser in fixed point. See diffuser_fixed.h.

#include "src/core/diffuser_fixed.h"

#include "src/core/dsp_fixed.h"

namespace op1clouds {

namespace {

static_assert(DF_L1_LEN == Scaled(126) && DF_L2_LEN == Scaled(180) && DF_L3_LEN == Scaled(269) &&
              DF_L4_LEN == Scaled(444) && DF_R1_LEN == Scaled(151) && DF_R2_LEN == Scaled(205) &&
              DF_R3_LEN == Scaled(245) && DF_R4_LEN == Scaled(405), "diffuser lengths");
static_assert(DF_L2 == DF_L1 + DF_L1_LEN + 1 && DF_L3 == DF_L2 + DF_L2_LEN + 1 &&
              DF_L4 == DF_L3 + DF_L3_LEN + 1 && DF_R1 == DF_L4 + DF_L4_LEN + 1 &&
              DF_R2 == DF_R1 + DF_R1_LEN + 1 && DF_R3 == DF_R2 + DF_R2_LEN + 1 &&
              DF_R4 == DF_R3 + DF_R3_LEN + 1, "diffuser bases");
static_assert(DF_MAX_REACH <= DF_SIZE, "diffuser memory full");

const int32_t kKap = 20480;   // 0.625, Q15

inline int64_t Fr(int32_t a, int32_t b) {
  if (a == -32768 && b == -32768) return 0x7fffffff;
  return static_cast<int64_t>(a * b) * 2;
}
inline int32_t Sat16(int64_t x) {
  return static_cast<int32_t>(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}
inline int32_t ExtractT(int64_t acc) { return Sat16(acc >> 16); }
inline int32_t Sat32(int64_t x) {
  return static_cast<int32_t>(x > INT32_MAX ? INT32_MAX : (x < INT32_MIN ? INT32_MIN : x));
}
inline int32_t Wrap32(int64_t x) { return static_cast<int32_t>(static_cast<uint32_t>(x)); }
inline int32_t Mul32x16(int32_t d, int32_t a) {
  const int64_t low = (static_cast<int64_t>(d & 0xffff) * a) >> 15;
  return Sat32(low + Fr(d >> 16, a));
}

// One side: four allpasses from `base1`, then the mix into *x.
inline void Side(int16_t* p, int32_t alias, int32_t* x, int32_t kap, int32_t minus_kap,
                 int32_t amount, const int32_t* bases, const int32_t* lengths) {
  // The input as Q14 (saturated), into the accumulator's high half.
  int64_t acc = static_cast<int64_t>(Sat32(static_cast<int64_t>(*x) << 3) >> 16) << 16;
  for (int32_t j = 0; j < 4; ++j) {
    const int32_t previous = p[bases[j] + lengths[j] - 1];
    acc += Fr(previous, kap);
    const int32_t w = ExtractT(acc);
    p[bases[j]] = static_cast<int16_t>(w);
    p[bases[j] + alias] = static_cast<int16_t>(w);
    acc = (static_cast<int64_t>(previous) << 16) + Fr(w, minus_kap);
  }
  const int32_t wet = ExtractT(acc) * 8192;   // Q14 -> Q27
  *x = Wrap32(static_cast<int64_t>(*x) + Mul32x16(Wrap32(static_cast<int64_t>(wet) - *x), amount));
}

const int32_t kBasesL[4] = { DF_L1, DF_L2, DF_L3, DF_L4 };
const int32_t kLengthsL[4] = { DF_L1_LEN, DF_L2_LEN, DF_L3_LEN, DF_L4_LEN };
const int32_t kBasesR[4] = { DF_R1, DF_R2, DF_R3, DF_R4 };
const int32_t kLengthsR[4] = { DF_R1_LEN, DF_R2_LEN, DF_R3_LEN, DF_R4_LEN };

}  // namespace

void DiffuserRunC(const DiffuserKernel* k, int32_t* l, int32_t* r, int16_t* p, int32_t n,
                  int32_t alias_bytes) {
  const int32_t alias = alias_bytes / 2;
  const int32_t kap = static_cast<int16_t>(k->kap & 0xffff);
  const int32_t minus_kap = static_cast<int16_t>(static_cast<uint32_t>(k->kap) >> 16);
  const int32_t amount = static_cast<int16_t>(k->amount & 0xffff);
  for (int32_t i = 0; i < n; ++i) {
    --p;
    Side(p, alias, l + i, kap, minus_kap, amount, kBasesL, kLengthsL);
    if (r) Side(p, alias, r + i, kap, minus_kap, amount, kBasesR, kLengthsR);
  }
}

void DiffuserFixed::Init() {
  amount_ = 0;
#if defined(__bfin__) && !defined(OP1CLOUDS_NO_ASM)
  use_asm_ = true;
#else
  use_asm_ = false;
#endif
  Clear();
}

void DiffuserFixed::Clear() {
  for (int32_t i = 0; i < DF_BUFFER_WORDS; ++i) buffer_[i] = 0;
  write_ptr_ = kSize;
}

void DiffuserFixed::Process(int32_t* l, int32_t* r, size_t size) {
  DiffuserKernel k;
  k.kap = (kKap & 0xffff) | (-kKap << 16);
  k.amount = amount_ >> 16;
  const int32_t n = static_cast<int32_t>(size);
  int32_t done = 0;
  while (done < n) {
    const int32_t next = write_ptr_ - 1;
    int32_t m = next + 1;                           // until the wrap
    const bool alias = next < DF_MAX_REACH;
    if (!alias) m = next - DF_MAX_REACH + 1;        // until the mirrored zone
    if (m > n - done) m = n - done;
    const int32_t alias_bytes = alias ? 2 * kSize : 0;
    int16_t* p = buffer_ + write_ptr_;
#if defined(__bfin__)
    if (use_asm_) {
      if (r) {
        DiffuserRunStereoAsm(&k, l + done, r + done, p, m, alias_bytes);
      } else {
        DiffuserRunMonoAsm(&k, l + done, p, m, alias_bytes);
      }
    } else {
      DiffuserRunC(&k, l + done, r ? r + done : nullptr, p, m, alias_bytes);
    }
#else
    DiffuserRunC(&k, l + done, r ? r + done : nullptr, p, m, alias_bytes);
#endif
    write_ptr_ -= m;
    if (write_ptr_ == 0) write_ptr_ = kSize;
    done += m;
  }
}

}  // namespace op1clouds
