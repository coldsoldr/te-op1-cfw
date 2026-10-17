// Forced include (-include) for the double-precision reference (make ref64):
// the system headers and the WAV writer are compiled first with the real
// float, then `float` becomes `double` for upstream's code.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <math.h>
#include <new>
#include <stddef.h>

#include "../../rings/harness/wav.h"

typedef float real_float;

// Upstream mixes float variables (now double) with float literals (0.49f).
namespace std {
inline double min(double a, real_float b) { return a < b ? a : static_cast<double>(b); }
inline double min(real_float a, double b) { return a < b ? static_cast<double>(a) : b; }
inline double max(double a, real_float b) { return a > b ? a : static_cast<double>(b); }
inline double max(real_float a, double b) { return a > b ? static_cast<double>(a) : b; }
}  // namespace std

#define REAL_FLOAT_DEFINED 1
#define float double
