#ifndef TREZOR_PC_MONITOR_PROTOCOL_H
#define TREZOR_PC_MONITOR_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TM_PROTOCOL_VERSION 1u
#define TM_PACKET_SIZE 64u
#define TM_PACKET_PAYLOAD_SIZE 52u
#define TM_MAX_PACK_SIZE 65536u
#define TM_MAX_SCREENS 8u
#define TM_MAX_WIDGETS_PER_SCREEN 24u
#define TM_MAX_CHANNELS 64u
#define TM_MAX_FONTS 8u
#define TM_MAX_IMAGES 64u
#define TM_MAX_ANIMATIONS 16u
#define TM_PACK_VERSION 1u

#define TM_WIDGET_FLAG_INVERTED 0x01u
#define TM_WIDGET_FLAG_AUTO_RANGE 0x02u
#define TM_WIDGET_FLAG_FPS_PRESETS 0x04u

#define TM_PACK_FLAG_PIXEL_SHIFT 0x01u
#define TM_PIXEL_SHIFT_INTERVAL_MS 120000u

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define TM_PACKED
#else
#define TM_PACKED __attribute__((packed))
#endif

typedef enum {
  TM_MSG_HELLO = 1,
  TM_MSG_CAPABILITIES = 2,
  TM_MSG_PACK_BEGIN = 3,
  TM_MSG_PACK_DATA = 4,
  TM_MSG_PACK_COMMIT = 5,
  TM_MSG_METRICS = 6,
  TM_MSG_BUTTON_EVENT = 7,
  TM_MSG_SET_PAGE = 8,
  TM_MSG_PING = 9,
  TM_MSG_PONG = 10,
  TM_MSG_REBOOT_BOOTLOADER = 11,
  TM_MSG_ACK = 126,
  TM_MSG_NACK = 127,
} tm_message_type_t;

typedef enum {
  TM_STATUS_VALID = 0,
  TM_STATUS_UNAVAILABLE = 1,
  TM_STATUS_STALE = 2,
  TM_STATUS_ESTIMATED = 4,
} tm_metric_status_t;

typedef enum {
  TM_ERROR_NONE = 0,
  TM_ERROR_BAD_MAGIC = 1,
  TM_ERROR_BAD_VERSION = 2,
  TM_ERROR_BAD_LENGTH = 3,
  TM_ERROR_BAD_CRC = 4,
  TM_ERROR_BAD_STATE = 5,
  TM_ERROR_BAD_OFFSET = 6,
  TM_ERROR_PACK_TOO_LARGE = 7,
  TM_ERROR_PACK_INVALID = 8,
  TM_ERROR_UNSUPPORTED = 9,
} tm_error_t;

typedef enum {
  TM_WIDGET_STATIC_TEXT = 1,
  TM_WIDGET_DYNAMIC_TEXT = 2,
  TM_WIDGET_IMAGE = 3,
  TM_WIDGET_ANIMATION = 4,
  TM_WIDGET_BAR_HORIZONTAL = 5,
  TM_WIDGET_BAR_VERTICAL = 6,
  TM_WIDGET_SEGMENTS = 7,
  TM_WIDGET_NEEDLE = 8,
  TM_WIDGET_RING = 9,
  TM_WIDGET_SPARKLINE = 10,
  TM_WIDGET_FRAME = 11,
  TM_WIDGET_LINE = 12,
  TM_WIDGET_CONDITIONAL_ICON = 13,
  TM_WIDGET_WARNING = 14,
} tm_widget_type_t;

typedef enum {
  TM_ACTION_NONE = 0,
  TM_ACTION_HOST = 1,
  TM_ACTION_NEXT_PAGE = 2,
  TM_ACTION_PREVIOUS_PAGE = 3,
  TM_ACTION_GO_TO_PAGE = 4,
} tm_action_type_t;

typedef enum {
  TM_BUTTON_LEFT = 0,
  TM_BUTTON_RIGHT = 1,
} tm_button_t;

typedef enum {
  TM_GESTURE_SHORT = 0,
  TM_GESTURE_LONG = 1,
} tm_gesture_t;

typedef struct TM_PACKED {
  uint8_t magic[2];
  uint8_t version;
  uint8_t type;
  uint8_t sequence;
  uint8_t flags;
  uint16_t payload_length;
  uint8_t payload[TM_PACKET_PAYLOAD_SIZE];
  uint32_t crc32;
} tm_packet_t;

typedef struct TM_PACKED {
  uint16_t channel_id;
  uint8_t status;
  int8_t scale_exponent;
  int32_t value;
} tm_metric_entry_t;

typedef struct TM_PACKED {
  uint8_t type;
  uint8_t target;
  uint16_t id;
} tm_action_t;

typedef struct TM_PACKED {
  uint8_t magic[4];
  uint16_t version;
  uint16_t header_size;
  uint32_t total_size;
  uint32_t crc32;
  uint8_t screen_count;
  uint8_t channel_count;
  uint8_t font_count;
  uint8_t image_count;
  uint8_t animation_count;
  uint8_t reserved[3];
  uint16_t widget_count;
  uint16_t reserved2;
  uint32_t screens_offset;
  uint32_t widgets_offset;
  uint32_t fonts_offset;
  uint32_t images_offset;
  uint32_t animations_offset;
  uint32_t resources_offset;
  uint32_t resources_size;
} tm_pack_header_t;

typedef struct TM_PACKED {
  uint32_t name_offset;
  uint16_t first_widget;
  uint8_t widget_count;
  uint8_t reserved;
  tm_action_t left_short;
  tm_action_t left_long;
  tm_action_t right_short;
  tm_action_t right_long;
} tm_screen_t;

typedef struct TM_PACKED {
  uint8_t type;
  uint8_t flags;
  uint8_t x;
  uint8_t y;
  uint8_t width;
  uint8_t height;
  uint16_t channel_id;
  int32_t minimum;
  int32_t maximum;
  uint32_t resource_offset;
  uint16_t resource_id;
  uint8_t font_id;
  uint8_t precision;
  uint16_t arg0;
  uint16_t arg1;
} tm_widget_t;

typedef struct TM_PACKED {
  uint16_t glyph_count;
  uint8_t height;
  uint8_t baseline;
  uint32_t glyphs_offset;
} tm_font_t;

typedef struct TM_PACKED {
  uint32_t codepoint;
  uint8_t width;
  uint8_t advance;
  int8_t bearing_x;
  int8_t bearing_y;
  uint16_t data_size;
  uint16_t reserved;
  uint32_t data_offset;
} tm_glyph_t;

typedef struct TM_PACKED {
  uint8_t width;
  uint8_t height;
  uint8_t encoding;
  uint8_t reserved;
  uint32_t data_offset;
  uint32_t data_size;
} tm_image_t;

typedef struct TM_PACKED {
  uint8_t width;
  uint8_t height;
  uint8_t frame_count;
  uint8_t fps;
  uint32_t frames_offset;
} tm_animation_t;

typedef struct TM_PACKED {
  uint32_t data_offset;
  uint16_t data_size;
  uint16_t duration_ms;
} tm_animation_frame_t;

typedef struct TM_PACKED {
  uint32_t event_id;
  uint16_t action_id;
  uint8_t page;
  uint8_t button;
  uint8_t gesture;
  uint8_t reserved[3];
} tm_button_event_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

uint32_t tm_crc32(const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
