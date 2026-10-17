// SVF coefficient tables (resources/gen_core_tables.py).

#ifndef OP1STREAMS_CORE_TABLES_H_
#define OP1STREAMS_CORE_TABLES_H_

#include "src/core/core_config.h"

namespace op1streams {

// [damping][cutoff code >> 8][a1, a2, a3], Q31; entry 256 is code 65536.
extern const int32_t kSvfTable[kNumDampings][257][3];

}  // namespace op1streams

#endif  // OP1STREAMS_CORE_TABLES_H_
