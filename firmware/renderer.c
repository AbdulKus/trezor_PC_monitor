#include "renderer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "monitor.h"
#include "oled.h"
#include "protocol.h"
#include "supervise.h"

#define FRAME_BYTES (OLED_WIDTH * OLED_HEIGHT / 8)

static bool dirty;
static bool blanked;
static uint32_t last_render;
static uint32_t last_activity;
static uint8_t animation_buffer[FRAME_BYTES];
static int32_t spark_history[TM_MAX_CHANNELS][16];
static uint32_t spark_history_time[TM_MAX_CHANNELS];
static bool spark_history_initialized[TM_MAX_CHANNELS];
static bool spark_scale_markers[TM_MAX_CHANNELS][16];
static int32_t spark_auto_targets[TM_MAX_CHANNELS];
static int32_t spark_auto_maximums[TM_MAX_CHANNELS];

static bool point_visible(int x, int y) {
  return x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT;
}

static void pixel(int x, int y, bool set) {
  if (!point_visible(x, y)) return;
  if (set)
    oledDrawPixel(x, y);
  else
    oledClearPixel(x, y);
}

static void line(int x0, int y0, int x1, int y1) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int sx = x0 < x1 ? 1 : -1;
  int dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
  int dy = -dy_abs;
  int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    pixel(x0, y0, true);
    if (x0 == x1 && y0 == y1) break;
    int twice = 2 * error;
    if (twice >= dy) {
      error += dy;
      x0 += sx;
    }
    if (twice <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

static const void *pack_at(uint32_t offset, uint32_t size) {
  const tm_pack_header_t *header = monitor_pack_header();
  if (!monitor_pack_active() || offset > header->total_size ||
      size > header->total_size - offset)
    return NULL;
  return monitor_pack() + offset;
}

static const char *pack_string(uint32_t offset) {
  const tm_pack_header_t *header = monitor_pack_header();
  if (!monitor_pack_active() || offset >= header->total_size) return "";
  const char *value = (const char *)(monitor_pack() + offset);
  for (uint32_t i = offset; i < header->total_size; i++) {
    if (monitor_pack()[i] == 0) return value;
  }
  return "";
}

static uint32_t utf8_next(const char **text) {
  const uint8_t *s = (const uint8_t *)*text;
  if (s[0] < 0x80) {
    (*text)++;
    return s[0];
  }
  if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
    *text += 2;
    return ((uint32_t)(s[0] & 0x1f) << 6) | (s[1] & 0x3f);
  }
  if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 &&
      (s[2] & 0xc0) == 0x80) {
    *text += 3;
    return ((uint32_t)(s[0] & 0x0f) << 12) |
           ((uint32_t)(s[1] & 0x3f) << 6) | (s[2] & 0x3f);
  }
  (*text)++;
  return '?';
}

static const tm_glyph_t *find_glyph(const tm_font_t *font,
                                    uint32_t codepoint) {
  const tm_glyph_t *glyphs =
      (const tm_glyph_t *)pack_at(font->glyphs_offset,
                                  font->glyph_count * sizeof(tm_glyph_t));
  if (glyphs == NULL) return NULL;
  for (uint16_t i = 0; i < font->glyph_count; i++) {
    if (glyphs[i].codepoint == codepoint) return &glyphs[i];
  }
  if (codepoint != '?') return find_glyph(font, '?');
  return NULL;
}

static void draw_custom_text(int x, int y, const char *text, uint8_t font_id) {
  const tm_pack_header_t *header = monitor_pack_header();
  if (font_id >= header->font_count) return;
  const tm_font_t *fonts = (const tm_font_t *)pack_at(
      header->fonts_offset, header->font_count * sizeof(tm_font_t));
  if (fonts == NULL) return;
  const tm_font_t *font = &fonts[font_id];
  while (*text != 0) {
    uint32_t codepoint = utf8_next(&text);
    const tm_glyph_t *glyph = find_glyph(font, codepoint);
    if (glyph == NULL) continue;
    const uint8_t *data =
        (const uint8_t *)pack_at(glyph->data_offset, glyph->data_size);
    if (data == NULL) continue;
    int stride = (glyph->width + 7) / 8;
    for (int gy = 0; gy < font->height; gy++) {
      for (int gx = 0; gx < glyph->width; gx++) {
        if ((data[gy * stride + gx / 8] & (0x80 >> (gx & 7))) != 0) {
          pixel(x + glyph->bearing_x + gx,
                y + font->baseline - glyph->bearing_y + gy, true);
        }
      }
    }
    x += glyph->advance;
    if (x >= OLED_WIDTH) break;
  }
}

static void draw_text(int x, int y, const char *text, uint8_t font_id) {
  if (font_id == 0xff)
    oledDrawString(x, y, text, FONT_STANDARD);
  else
    draw_custom_text(x, y, text, font_id);
}

static void append_char(char *output, uint32_t capacity, uint32_t *length,
                        char value) {
  if (*length + 1 < capacity) output[(*length)++] = value;
}

static void append_text(char *output, uint32_t capacity, uint32_t *length,
                        const char *value) {
  while (*value != 0) append_char(output, capacity, length, *value++);
}

static void format_number(const tm_metric_entry_t *metric, uint8_t precision,
                          char output[32]) {
  if (metric == NULL ||
      (metric->status & (TM_STATUS_UNAVAILABLE | TM_STATUS_STALE)) != 0) {
    memcpy(output, "--", 3);
    return;
  }
  int64_t value = metric->value;
  bool negative = value < 0;
  if (negative) value = -value;
  int decimals = metric->scale_exponent < 0 ? -metric->scale_exponent : 0;
  if (decimals > 6) decimals = 6;
  if (precision < decimals) decimals = precision;
  for (int i = 0; i < metric->scale_exponent; i++) value *= 10;
  int64_t divisor = 1;
  int source_decimals = metric->scale_exponent < 0 ? -metric->scale_exponent : 0;
  for (int i = 0; i < source_decimals; i++) divisor *= 10;
  int64_t integer = value / divisor;
  int64_t fraction = value % divisor;
  while (source_decimals > decimals) {
    fraction /= 10;
    source_decimals--;
  }
  char reverse[20];
  int reverse_len = 0;
  do {
    reverse[reverse_len++] = (char)('0' + integer % 10);
    integer /= 10;
  } while (integer != 0 && reverse_len < (int)sizeof(reverse));
  uint32_t length = 0;
  if (negative) output[length++] = '-';
  while (reverse_len > 0) output[length++] = reverse[--reverse_len];
  if (decimals > 0) {
    output[length++] = '.';
    int64_t place = 1;
    for (int i = 1; i < decimals; i++) place *= 10;
    for (int i = 0; i < decimals; i++) {
      output[length++] = (char)('0' + (fraction / place) % 10);
      place /= 10;
    }
  }
  output[length] = 0;
}

static void format_template(const char *format, const char *value,
                            char output[48]) {
  uint32_t length = 0;
  while (*format != 0 && length + 1 < 48) {
    if (format[0] == '{' && format[1] == 'v' && format[2] == '}') {
      append_text(output, 48, &length, value);
      format += 3;
    } else {
      append_char(output, 48, &length, *format++);
    }
  }
  output[length] = 0;
}

// Widget bounds are stored as centi-units so every sensor can use the same
// comparison code while HID values retain their own decimal exponent.
static int32_t metric_centi_value(const tm_widget_t *widget, bool *valid) {
  const tm_metric_entry_t *metric = monitor_metric(widget->channel_id);
  *valid = metric != NULL &&
           (metric->status & (TM_STATUS_UNAVAILABLE | TM_STATUS_STALE)) == 0;
  if (!*valid) return 0;
  int64_t value = metric->value;
  int exponent = metric->scale_exponent;
  while (exponent < -2) {
    value /= 10;
    exponent++;
  }
  while (exponent > -2) {
    value *= 10;
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    exponent--;
  }
  return (int32_t)value;
}

static int range_amount(int32_t value, int32_t minimum, int32_t maximum) {
  if (maximum <= minimum) return 500;
  if (value <= minimum) return 0;
  if (value >= maximum) return 1000;
  return (int)(((int64_t)value - minimum) * 1000 /
               ((int64_t)maximum - minimum));
}

// Keep zero at the bottom and round the upper bound to a calm, readable value.
// The 15% headroom prevents ordinary sample noise from touching the top edge.
static int32_t spark_nice_maximum(int32_t peak) {
  if (peak <= 0) return 100;
  int64_t padded = ((int64_t)peak * 115 + 99) / 100;
  int64_t magnitude = 1;
  while (padded >= magnitude * 10 && magnitude <= INT32_MAX / 10)
    magnitude *= 10;
  int64_t step = magnitude / 10;
  if (step < 1) step = 1;
  int64_t result = ((padded + step - 1) / step) * step;
  return result > INT32_MAX ? INT32_MAX : (int32_t)result;
}

static int gauge_amount(const tm_widget_t *widget) {
  bool valid;
  int32_t value = metric_centi_value(widget, &valid);
  if (!valid || widget->maximum <= widget->minimum) return 0;
  return range_amount(value, widget->minimum, widget->maximum);
}

static bool decode_rle(const uint8_t *encoded, uint32_t encoded_size,
                       uint8_t *output, uint32_t output_size, bool xor_mode) {
  uint32_t source = 0;
  uint32_t target = 0;
  while (source + 1 < encoded_size && target < output_size) {
    uint32_t count = encoded[source++];
    uint8_t value = encoded[source++];
    if (count == 0 || count > output_size - target) return false;
    for (uint32_t i = 0; i < count; i++) {
      if (xor_mode)
        output[target++] ^= value;
      else
        output[target++] = value;
    }
  }
  return target == output_size;
}

static void draw_bits_scaled(int x, int y, int source_width, int source_height,
                             int target_width, int target_height,
                             const uint8_t *data) {
  int stride = (source_width + 7) / 8;
  if (target_width <= 0 || target_height <= 0) return;
  for (int py = 0; py < target_height; py++) {
    int source_y = py * source_height / target_height;
    for (int px = 0; px < target_width; px++) {
      int source_x = px * source_width / target_width;
      if ((data[source_y * stride + source_x / 8] &
           (0x80 >> (source_x & 7))) != 0)
        pixel(x + px, y + py, true);
    }
  }
}

static void draw_image(const tm_widget_t *widget) {
  const tm_pack_header_t *header = monitor_pack_header();
  if (widget->resource_id >= header->image_count) return;
  const tm_image_t *images = (const tm_image_t *)pack_at(
      header->images_offset, header->image_count * sizeof(tm_image_t));
  if (images == NULL) return;
  const tm_image_t *image = &images[widget->resource_id];
  const uint8_t *data =
      (const uint8_t *)pack_at(image->data_offset, image->data_size);
  if (data == NULL) return;
  uint32_t decoded_size = (uint32_t)((image->width + 7) / 8) * image->height;
  if (decoded_size > sizeof(animation_buffer)) return;
  if (image->encoding == 0) {
    if (image->data_size < decoded_size) return;
    draw_bits_scaled(widget->x, widget->y, image->width, image->height,
                     widget->width, widget->height, data);
  } else {
    memset(animation_buffer, 0, decoded_size);
    if (decode_rle(data, image->data_size, animation_buffer, decoded_size,
                   false))
      draw_bits_scaled(widget->x, widget->y, image->width, image->height,
                       widget->width, widget->height, animation_buffer);
  }
}

static void draw_animation(const tm_widget_t *widget, uint32_t now_ms) {
  const tm_pack_header_t *header = monitor_pack_header();
  if (widget->resource_id >= header->animation_count) return;
  const tm_animation_t *animations = (const tm_animation_t *)pack_at(
      header->animations_offset,
      header->animation_count * sizeof(tm_animation_t));
  if (animations == NULL) return;
  const tm_animation_t *animation = &animations[widget->resource_id];
  const tm_animation_frame_t *frames =
      (const tm_animation_frame_t *)pack_at(
          animation->frames_offset,
          animation->frame_count * sizeof(tm_animation_frame_t));
  if (frames == NULL) return;
  uint32_t cycle_duration = 0;
  for (uint32_t i = 0; i < animation->frame_count; i++)
    cycle_duration += frames[i].duration_ms == 0 ? 100 : frames[i].duration_ms;
  uint32_t position = cycle_duration == 0 ? 0 : now_ms % cycle_duration;
  uint32_t frame_index = 0;
  for (uint32_t i = 0; i < animation->frame_count; i++) {
    uint32_t duration = frames[i].duration_ms == 0 ? 100 : frames[i].duration_ms;
    frame_index = i;
    if (position < duration) break;
    position -= duration;
  }
  uint32_t decoded_size =
      (uint32_t)((animation->width + 7) / 8) * animation->height;
  if (decoded_size > sizeof(animation_buffer)) return;
  memset(animation_buffer, 0, decoded_size);
  for (uint32_t i = 0; i <= frame_index; i++) {
    const uint8_t *data =
        (const uint8_t *)pack_at(frames[i].data_offset, frames[i].data_size);
    if (data == NULL || !decode_rle(data, frames[i].data_size,
                                    animation_buffer, decoded_size, true))
      return;
  }
  draw_bits_scaled(widget->x, widget->y, animation->width, animation->height,
                   widget->width, widget->height, animation_buffer);
}

static void draw_ring(const tm_widget_t *widget, int amount) {
  int cx = widget->x + widget->width / 2;
  int cy = widget->y + widget->height / 2;
  int radius = widget->width < widget->height ? widget->width / 2
                                             : widget->height / 2;
  if (radius > 15) radius = 15;
  static const int8_t circle[32][2] = {
      {0, -16},  {3, -16},  {6, -15},  {9, -13},  {11, -11}, {13, -9},
      {15, -6},  {16, -3},  {16, 0},   {16, 3},   {15, 6},   {13, 9},
      {11, 11},  {9, 13},   {6, 15},   {3, 16},   {0, 16},   {-3, 16},
      {-6, 15},  {-9, 13},  {-11, 11}, {-13, 9},  {-15, 6},  {-16, 3},
      {-16, 0},  {-16, -3}, {-15, -6}, {-13, -9}, {-11, -11}, {-9, -13},
      {-6, -15}, {-3, -16}};
  int segments = amount * 32 / 1000;
  for (int i = 0; i < segments; i++) {
    int next = (i + 1) & 31;
    line(cx + circle[i][0] * radius / 16,
         cy + circle[i][1] * radius / 16,
         cx + circle[next][0] * radius / 16,
         cy + circle[next][1] * radius / 16);
    if (radius > 3) {
      line(cx + circle[i][0] * (radius - 2) / 16,
           cy + circle[i][1] * (radius - 2) / 16,
           cx + circle[next][0] * (radius - 2) / 16,
           cy + circle[next][1] * (radius - 2) / 16);
    }
  }
}

static void draw_widget(const tm_widget_t *widget, uint32_t now_ms) {
  int x1 = widget->x;
  int y1 = widget->y;
  int x2 = x1 + widget->width - 1;
  int y2 = y1 + widget->height - 1;
  switch (widget->type) {
    case TM_WIDGET_STATIC_TEXT:
      draw_text(x1, y1, pack_string(widget->resource_offset), widget->font_id);
      break;
    case TM_WIDGET_DYNAMIC_TEXT: {
      char value[32];
      char formatted[48];
      format_number(monitor_metric(widget->channel_id), widget->precision,
                    value);
      format_template(pack_string(widget->resource_offset), value, formatted);
      draw_text(x1, y1, formatted, widget->font_id);
      break;
    }
    case TM_WIDGET_IMAGE:
      draw_image(widget);
      break;
    case TM_WIDGET_ANIMATION:
      draw_animation(widget, now_ms);
      break;
    case TM_WIDGET_BAR_HORIZONTAL: {
      int amount = gauge_amount(widget);
      oledFrame(x1, y1, x2, y2);
      int fill = (widget->width - 2) * amount / 1000;
      if (fill > 0) oledBox(x1 + 1, y1 + 1, x1 + fill, y2 - 1, true);
      break;
    }
    case TM_WIDGET_BAR_VERTICAL: {
      int amount = gauge_amount(widget);
      oledFrame(x1, y1, x2, y2);
      int fill = (widget->height - 2) * amount / 1000;
      if (fill > 0) oledBox(x1 + 1, y2 - fill, x2 - 1, y2 - 1, true);
      break;
    }
    case TM_WIDGET_SEGMENTS: {
      int segments = widget->arg0 == 0 ? 5 : widget->arg0;
      if (segments > widget->width) segments = widget->width;
      int amount = gauge_amount(widget);
      int lit = (amount * segments + 500) / 1000;
      int gap = segments > 1 ? 1 : 0;
      int available = widget->width - gap * (segments - 1);
      int base = available / segments;
      int remainder = available % segments;
      int cursor = x1;
      for (int i = 0; i < segments; i++) {
        int width = base + (i < remainder ? 1 : 0);
        int sx1 = cursor;
        int sx2 = cursor + width - 1;
        if (i < lit)
          oledBox(sx1, y1, sx2, y2, true);
        else
          oledFrame(sx1, y1, sx2, y2);
        cursor = sx2 + 1 + gap;
      }
      break;
    }
    case TM_WIDGET_NEEDLE: {
      int amount = gauge_amount(widget);
      int cx = x1 + widget->width / 2;
      int cy = y2;
      int dx = (amount - 500) * (widget->width / 2) / 500;
      int rise = widget->height - 1;
      int abs_dx = dx < 0 ? -dx : dx;
      int dy = rise - abs_dx * rise / (widget->width / 2 + 1);
      line(cx, cy, cx + dx, cy - dy);
      oledFrame(x1, y1, x2, y2);
      break;
    }
    case TM_WIDGET_RING:
      draw_ring(widget, gauge_amount(widget));
      break;
    case TM_WIDGET_SPARKLINE: {
      bool valid;
      int32_t value = metric_centi_value(widget, &valid);
      const uint16_t channel = widget->channel_id;
      if (!spark_history_initialized[channel] && valid) {
        for (int i = 0; i < 16; i++) spark_history[channel][i] = value;
        memset(spark_scale_markers[channel], 0,
               sizeof(spark_scale_markers[channel]));
        spark_history_initialized[channel] = true;
        spark_history_time[channel] = now_ms;
        spark_auto_targets[channel] = spark_nice_maximum(value);
        spark_auto_maximums[channel] = spark_auto_targets[channel];
      }
      if (spark_history_initialized[channel] && valid &&
          now_ms - spark_history_time[channel] >= 250) {
        memmove(&spark_history[channel][0], &spark_history[channel][1],
                15 * sizeof(int32_t));
        memmove(&spark_scale_markers[channel][0],
                &spark_scale_markers[channel][1], 15 * sizeof(bool));
        spark_history[channel][15] = value;
        int32_t peak = 0;
        for (int i = 0; i < 16; i++)
          if (spark_history[channel][i] > peak) peak = spark_history[channel][i];
        int32_t next_target = spark_nice_maximum(peak);
        spark_scale_markers[channel][15] =
            spark_auto_targets[channel] != 0 &&
            next_target != spark_auto_targets[channel];
        spark_auto_targets[channel] = next_target;
        spark_history_time[channel] = now_ms;
      }
      if (!spark_history_initialized[channel]) break;
      int32_t minimum = widget->minimum;
      int32_t maximum = widget->maximum;
      if ((widget->flags & TM_WIDGET_FLAG_AUTO_RANGE) != 0) {
        minimum = 0;
        int32_t displayed = spark_auto_maximums[channel];
        int32_t target = spark_auto_targets[channel];
        if (displayed < target) {
          int64_t difference = (int64_t)target - displayed;
          displayed += (int32_t)((difference + 1) / 2);
        } else if (displayed > target) {
          int64_t difference = (int64_t)displayed - target;
          displayed -= (int32_t)((difference + 31) / 32);
        }
        if (displayed < 1) displayed = 1;
        spark_auto_maximums[channel] = displayed;
        maximum = displayed;
        for (int i = 0; i < 16; i++) {
          if (!spark_scale_markers[channel][i]) continue;
          int px = x1 + i * (widget->width - 1) / 15;
          for (int py = y1; py <= y2; py += 3) pixel(px, py, true);
        }
      }
      for (int i = 1; i < 16; i++) {
        int amount0 = range_amount(spark_history[channel][i - 1],
                                   minimum, maximum);
        int amount1 = range_amount(spark_history[channel][i],
                                   minimum, maximum);
        int px0 = x1 + (i - 1) * (widget->width - 1) / 15;
        int px1 = x1 + i * (widget->width - 1) / 15;
        int py0 = y2 - amount0 * (widget->height - 1) / 1000;
        int py1 = y2 - amount1 * (widget->height - 1) / 1000;
        line(px0, py0, px1, py1);
      }
      break;
    }
    case TM_WIDGET_FRAME:
      oledFrame(x1, y1, x2, y2);
      break;
    case TM_WIDGET_LINE:
      line(x1, y1, x2, y2);
      break;
    case TM_WIDGET_CONDITIONAL_ICON: {
      bool valid;
      int32_t value = metric_centi_value(widget, &valid);
      if (valid && value >= widget->minimum) draw_image(widget);
      break;
    }
    case TM_WIDGET_WARNING: {
      bool valid;
      int32_t value = metric_centi_value(widget, &valid);
      if (valid && value >= widget->minimum && ((now_ms / 400) & 1) != 0) {
        oledInvert(x1, y1, x2, y2);
        draw_text(x1 + 2, y1 + 1, pack_string(widget->resource_offset),
                  widget->font_id);
      }
      break;
    }
    default:
      break;
  }
}

static void render_page(uint32_t now_ms) {
  const tm_pack_header_t *header = monitor_pack_header();
  const tm_screen_t *screens = (const tm_screen_t *)pack_at(
      header->screens_offset, header->screen_count * sizeof(tm_screen_t));
  const tm_widget_t *widgets = (const tm_widget_t *)pack_at(
      header->widgets_offset, header->widget_count * sizeof(tm_widget_t));
  if (screens == NULL || widgets == NULL) return;
  const tm_screen_t *screen = &screens[monitor_current_page()];
  oledClear();
  for (uint8_t i = 0; i < screen->widget_count; i++) {
    draw_widget(&widgets[screen->first_widget + i], now_ms);
  }
  oledRefresh();
}

void renderer_init(void) {
  memset(spark_history, 0, sizeof(spark_history));
  memset(spark_history_time, 0, sizeof(spark_history_time));
  memset(spark_history_initialized, 0, sizeof(spark_history_initialized));
  memset(spark_scale_markers, 0, sizeof(spark_scale_markers));
  memset(spark_auto_targets, 0, sizeof(spark_auto_targets));
  memset(spark_auto_maximums, 0, sizeof(spark_auto_maximums));
  dirty = true;
  blanked = false;
  last_render = 0;
  last_activity = svc_timer_ms();
}

void renderer_invalidate(void) {
  dirty = true;
  blanked = false;
  last_activity = svc_timer_ms();
}

void renderer_waiting(void) {
  oledClear();
  oledFrame(0, 0, 127, 63);
  oledDrawStringCenter(64, 7, "PC MONITOR", FONT_STANDARD);
  oledDrawStringCenter(64, 24, "WAITING FOR PC", FONT_STANDARD);
  oledDrawStringCenter(64, 41, "OPEN TREZOR MONITOR", FONT_STANDARD);
  oledDrawStringCenter(64, 53, "USB READY", FONT_STANDARD);
  oledRefresh();
}

void renderer_system(void) {
  oledClear();
  oledFrame(0, 0, 127, 63);
  oledDrawStringCenter(64, 3, "SYSTEM", FONT_STANDARD);
  oledDrawString(5, 17, "USB: CONNECTED", FONT_STANDARD);
  oledDrawString(5, 29, "PROTOCOL: 1", FONT_STANDARD);
  oledDrawString(5, 41, "< BACK", FONT_STANDARD);
  oledDrawStringRight(123, 51, "> HOLD: BOOT", FONT_STANDARD);
  oledRefresh();
}

void renderer_tick(uint32_t now_ms) {
  if (monitor_system_visible() || !monitor_pack_active()) return;
  if (!blanked && now_ms - last_activity > 600000) {
    oledClear();
    oledRefresh();
    blanked = true;
    return;
  }
  bool animation_due = now_ms - last_render >= 83;
  if ((dirty && now_ms - last_render >= 50) || animation_due) {
    render_page(now_ms);
    last_render = now_ms;
    dirty = false;
  }
}
