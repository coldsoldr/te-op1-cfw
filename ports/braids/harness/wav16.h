// Minimal mono 16-bit PCM WAV writer (no dependencies).

#ifndef PORTS_BRAIDS_HARNESS_WAV16_H_
#define PORTS_BRAIDS_HARNESS_WAV16_H_

#include <cstdint>
#include <cstdio>

namespace harness {

class Wav16Writer {
 public:
  Wav16Writer() : fp_(NULL), frames_(0), sample_rate_(0) { }
  ~Wav16Writer() { Close(); }

  bool Open(const char* path, uint32_t sample_rate) {
    fp_ = fopen(path, "wb");
    if (!fp_) return false;
    sample_rate_ = sample_rate;
    frames_ = 0;
    WriteHeader();
    return true;
  }

  void Write(const int16_t* samples, size_t size) {
    if (!fp_) return;
    fwrite(samples, 2, size, fp_);   // little-endian hosts only
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
    uint32_t data_bytes = frames_ * 2;
    fwrite("RIFF", 1, 4, fp_); U32(36 + data_bytes);
    fwrite("WAVE", 1, 4, fp_);
    fwrite("fmt ", 1, 4, fp_); U32(16);
    U16(1);  // PCM
    U16(1); U32(sample_rate_); U32(sample_rate_ * 2); U16(2); U16(16);
    fwrite("data", 1, 4, fp_); U32(data_bytes);
  }

  FILE* fp_;
  uint32_t frames_;
  uint32_t sample_rate_;
};

}  // namespace harness

#endif  // PORTS_BRAIDS_HARNESS_WAV16_H_
