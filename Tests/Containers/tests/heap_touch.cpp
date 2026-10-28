#include <stddef.h>
#include <stdint.h>
extern "C" __declspec(noinline) uint64_t heap_touch(const char *data,
                                                    size_t size) {
  uint64_t result = 0;
  for (size_t i = 0; i < size; ++i)
    result = result * 131 + static_cast<unsigned char>(data[i]);
  return result;
}
