// Grids drum-map and Euclidean tables (const data, generated: see
// scripts/gen_tables.py). Grids is (c) 2011-2012 Emilie Gillet, GPL v3.

#ifndef PORTS_GRIDS_SRC_GRIDS_TABLES_H_
#define PORTS_GRIDS_SRC_GRIDS_TABLES_H_

#include <stdint.h>

namespace op1grids {

const uint8_t kNumNodes = 25;
const uint8_t kNodeSize = 96;  // 3 parts x 32 steps

extern const uint8_t kDrumNodes[kNumNodes][kNodeSize];
extern const uint32_t kEuclidean[1024];

// Upstream's drum_map[5][5] of node pointers, as node indices (no pointers:
// nothing to relocate). drum_map[i][j]: i from X, j from Y.
static const uint8_t kDrumMap[5][5] = {
  { 10, 8, 0, 9, 11 },
  { 15, 7, 13, 12, 6 },
  { 18, 14, 4, 5, 3 },
  { 23, 16, 21, 1, 2 },
  { 24, 19, 17, 20, 22 },
};

}  // namespace op1grids

#endif  // PORTS_GRIDS_SRC_GRIDS_TABLES_H_
