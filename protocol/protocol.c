#include "protocol.h"

uint32_t tm_crc32(const uint8_t *data, uint32_t size) {
  uint32_t crc = 0xffffffffu;
  for (uint32_t i = 0; i < size; i++) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; bit++) {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}
