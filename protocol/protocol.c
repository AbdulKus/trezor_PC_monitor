#include "protocol.h"

#include <stddef.h>

static uint64_t tm_power10(unsigned int power) {
  uint64_t result = 1;
  for (unsigned int i = 0; i < power; i++) result *= 10;
  return result;
}

static void tm_format_unavailable(char output[32]) {
  output[0] = '-';
  output[1] = '-';
  output[2] = 0;
}

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

void tm_format_metric(const tm_metric_entry_t *metric, uint8_t precision,
                      char output[32]) {
  if (metric == NULL ||
      (metric->status & (TM_STATUS_UNAVAILABLE | TM_STATUS_STALE)) != 0) {
    tm_format_unavailable(output);
    return;
  }

  if (precision > 6) precision = 6;
  const int shift = (int)metric->scale_exponent + (int)precision;
  const int negative = metric->value < 0;
  const uint64_t magnitude = negative
                                 ? (uint64_t)(-(int64_t)metric->value)
                                 : (uint64_t)metric->value;
  uint64_t scaled = 0;
  if (shift >= 0) {
    if (shift > 18) {
      tm_format_unavailable(output);
      return;
    }
    const uint64_t factor = tm_power10((unsigned int)shift);
    if (magnitude > UINT64_MAX / factor) {
      tm_format_unavailable(output);
      return;
    }
    scaled = magnitude * factor;
  } else {
    const unsigned int dropped = (unsigned int)-shift;
    if (dropped <= 18) {
      const uint64_t divisor = tm_power10(dropped);
      // Round half away from zero, matching the fixed-point desktop preview.
      scaled = (magnitude + divisor / 2) / divisor;
    }
  }

  char reverse[24];
  int digit_count = 0;
  do {
    reverse[digit_count++] = (char)('0' + scaled % 10);
    scaled /= 10;
  } while (scaled != 0 && digit_count < (int)sizeof(reverse));

  uint32_t length = 0;
  if (negative) output[length++] = '-';
  if (precision == 0) {
    while (digit_count > 0) output[length++] = reverse[--digit_count];
  } else if (digit_count <= precision) {
    output[length++] = '0';
    output[length++] = '.';
    for (int i = digit_count; i < precision; i++) output[length++] = '0';
    while (digit_count > 0) output[length++] = reverse[--digit_count];
  } else {
    while (digit_count > precision)
      output[length++] = reverse[--digit_count];
    output[length++] = '.';
    while (digit_count > 0) output[length++] = reverse[--digit_count];
  }
  output[length] = 0;
}
