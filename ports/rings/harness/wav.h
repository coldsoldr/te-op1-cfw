// Minimal stereo 32-bit float WAV writer (no dependencies).

#ifndef PORTS_RINGS_HARNESS_WAV_H_
#define PORTS_RINGS_HARNESS_WAV_H_

#include <cstdint>
#include <cstdio>

namespace harness {

class FloatWavWriter {
 public:
  FloatWavWriter() : fp_(NULL), frames_(0) { }
  ~FloatWavWriter() { Close(); }

  bool Open(const char* path, uint32_t sample_rate) {
    fp_ = fopen(path, "wb");
    if (!fp_) return false;
    sample_rate_ = sample_rate;
    WriteHeader();
    return true;
  }

  void Write(const float* l, const float* r, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      fwrite(&l[i], 4, 1, fp_);
      fwrite(&r[i], 4, 1, fp_);
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
    uint32_t data_bytes = frames_ * 8;
    fwrite("RIFF", 1, 4, fp_); U32(36 + data_bytes);
    fwrite("WAVE", 1, 4, fp_);
    fwrite("fmt ", 1, 4, fp_); U32(16);
    U16(3);  // IEEE float
    U16(2); U32(sample_rate_); U32(sample_rate_ * 8); U16(8); U16(32);
    fwrite("data", 1, 4, fp_); U32(data_bytes);
  }

  FILE* fp_;
  uint32_t sample_rate_;
  uint32_t frames_;
};

}  // namespace harness

#endif  // PORTS_RINGS_HARNESS_WAV_H_
