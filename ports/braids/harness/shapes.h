// Braids' shapes: the module's four-letter display names (settings.cc,
// with its custom glyphs spelled out) and which shapes have a steady pitch
// (the pitch check measures only those).

#ifndef PORTS_BRAIDS_HARNESS_SHAPES_H_
#define PORTS_BRAIDS_HARNESS_SHAPES_H_

namespace harness {

const int kNumShapes = 48;   // MACRO_OSC_SHAPE_LAST; 47 is the easter egg

const char* const kShapeNames[kNumShapes] = {
  "CSAW", "MRPH", "SAWSQ", "FOLD", "BUZZ",
  "SUBSQ", "SUBSAW", "SYNSQ", "SYNSAW",
  "SAWx3", "SQx3", "TRIx3", "SINx3", "RING",
  "SWARM", "COMB", "TOY",
  "ZLPF", "ZPKF", "ZBPF", "ZHPF",
  "VOSM", "VOWL", "VFOF", "HARM",
  "FM", "FBFM", "WTFM",
  "PLUK", "BOWD", "BLOW", "FLUT",
  "BELL", "DRUM", "KICK", "CYMB", "SNAR",
  "WTBL", "WMAP", "WLIN", "WTx4",
  "NOIS", "TWNQ", "CLKN", "CLOU", "PRTC",
  "QPSK", "????",
};

// 1 if the shape has a steady fundamental at the played pitch with the
// pitch check's settings (timbre and color at 16384, BUZZ's color at 0).
// Noise, drums and the easter egg are skipped; so are shapes whose
// fundamental isn't the played note by design (CYMB plays (40 << 7) +
// pitch / 2, WTFM's chaotic modulator).
const unsigned char kPitched[kNumShapes] = {
  1, 1, 1, 1, 1,
  1, 1, 1, 1,
  1, 1, 1, 1, 1,
  1, 1, 1,
  1, 1, 1, 1,
  1, 1, 1, 1,
  1, 1, 0,
  1, 1, 1, 1,
  0, 0, 0, 0, 0,
  1, 1, 1, 1,
  0, 0, 0, 0, 0,
  1, 0,
};

}  // namespace harness

#endif  // PORTS_BRAIDS_HARNESS_SHAPES_H_
