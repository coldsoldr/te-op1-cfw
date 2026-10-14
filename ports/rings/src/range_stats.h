// Host-only instrumentation: records the largest and smallest non-zero
// magnitude seen per named signal, to choose fixed-point formats. Enabled with
// -DOP1RINGS_RANGE_STATS; compiles to nothing otherwise.

#ifndef OP1RINGS_RANGE_STATS_H_
#define OP1RINGS_RANGE_STATS_H_

#ifdef OP1RINGS_RANGE_STATS

#include <cmath>
#include <cstdio>
#include <cstring>

namespace op1rings {

struct RangeStat {
  const char* name;
  double max_abs;
  double min_abs_nonzero;
  unsigned long count;
};

class RangeStats {
 public:
  static RangeStat* Get(const char* name) {
    for (int i = 0; i < num_; ++i) {
      if (strcmp(stats_[i].name, name) == 0) return &stats_[i];
    }
    RangeStat* s = &stats_[num_++];
    s->name = name;
    s->max_abs = 0.0;
    s->min_abs_nonzero = 1e30;
    s->count = 0;
    return s;
  }

  static void Record(const char* name, double value) {
    RangeStat* s = Get(name);
    double a = fabs(value);
    if (a > s->max_abs) s->max_abs = a;
    if (a > 0.0 && a < s->min_abs_nonzero) s->min_abs_nonzero = a;
    ++s->count;
  }

  static void Print(const char* title) {
    printf("-- ranges: %s\n", title);
    for (int i = 0; i < num_; ++i) {
      const RangeStat& s = stats_[i];
      printf("  %-28s max %12.6g (2^%5.1f)  min nonzero %12.6g (2^%6.1f)  n %lu\n",
             s.name, s.max_abs, s.max_abs > 0 ? log2(s.max_abs) : -999.0,
             s.min_abs_nonzero, s.min_abs_nonzero < 1e30 ? log2(s.min_abs_nonzero) : 999.0,
             s.count);
    }
  }

  static void Reset() { num_ = 0; }

 private:
  static RangeStat stats_[64];
  static int num_;
};

}  // namespace op1rings

#define OP1RINGS_RANGE(name, value) ::op1rings::RangeStats::Record(name, value)
#define OP1RINGS_RANGE_DEFINE \
  ::op1rings::RangeStat op1rings::RangeStats::stats_[64]; \
  int op1rings::RangeStats::num_ = 0;

#else

#define OP1RINGS_RANGE(name, value) do { } while (0)
#define OP1RINGS_RANGE_DEFINE

#endif  // OP1RINGS_RANGE_STATS

#endif  // OP1RINGS_RANGE_STATS_H_
