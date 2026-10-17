// Minimal stereo 16-bit PCM WAV writer (no dependencies). Clouds' input and
// output are int16 frames, so the harness writes exactly what the
// processors produce.

#ifndef PORTS_CLOUDS_HARNESS_WAV16_H_
#define PORTS_CLOUDS_HARNESS_WAV16_H_

#include <cstdint>
#include <cstdio>

namespace harness {

class Wav16Writer {
 public:
  Wav16Writer() : fp_(NULL), frames_(0) { }
  ~Wav16Writer() { Close(); }

  bool Open(const char* path, uint32_t sample_rate) {
    fp_ = fopen(path, "wb");
    if (!fp_) return false;
    sample_rate_ = sample_rate;
    frames_ = 0;
    WriteHeader();
    return true;
  }

  void Write(const int16_t* l, const int16_t* r, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      fwrite(&l[i], 2, 1, fp_);
      fwrite(&r[i], 2, 1, fp_);
    }
    frames_ += size;
  }

  void Close() {
    if (!fp_) return;
    fseek(fp_, 0, SEEK_SET);
    WriteHeader();
    fclose(fp_);
    fp_ = NULL;
  }

 private:
  void U32(uint32_t v) { fwrite(&v, 4, 1, fp_); }
  void U16(uint16_t v) { fwrite(&v, 2, 1, fp_); }

  void WriteHeader() {
    uint32_t data_bytes = frames_ * 4;
    fwrite("RIFF", 1, 4, fp_); U32(36 + data_bytes);
    fwrite("WAVE", 1, 4, fp_);
    fwrite("fmt ", 1, 4, fp_); U32(16);
    U16(1);  // PCM
    U16(2); U32(sample_rate_); U32(sample_rate_ * 4); U16(4); U16(16);
    fwrite("data", 1, 4, fp_); U32(data_bytes);
  }

  FILE* fp_;
  uint32_t sample_rate_;
  uint32_t frames_;
};

}  // namespace harness

#endif  // PORTS_CLOUDS_HARNESS_WAV16_H_
