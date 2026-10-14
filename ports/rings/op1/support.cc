// Minimal runtime for injected code: GCC may emit calls to these for struct
// copies and array clears. Everything else comes from libgcc.

#include <cstddef>

extern "C" {

void* memset(void* dst, int value, size_t n) {
  unsigned char* d = static_cast<unsigned char*>(dst);
  while (n--) *d++ = static_cast<unsigned char>(value);
  return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = static_cast<unsigned char*>(dst);
  const unsigned char* s = static_cast<const unsigned char*>(src);
  while (n--) *d++ = *s++;
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = static_cast<unsigned char*>(dst);
  const unsigned char* s = static_cast<const unsigned char*>(src);
  if (d < s) {
    while (n--) *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n--) *--d = *--s;
  }
  return dst;
}

}  // extern "C"
