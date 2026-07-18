#include "monitor.h"

#include <stddef.h>
#include <string.h>

#include "renderer.h"
#include "supervise.h"

#if defined(_MSC_VER)
typedef char tm_packet_size_must_be_64[
    sizeof(tm_packet_t) == TM_PACKET_SIZE ? 1 : -1];
#else
_Static_assert(sizeof(tm_packet_t) == TM_PACKET_SIZE, "packet size");
#endif

#if defined(_MSC_VER)
__declspec(align(4)) static uint8_t pack_data[TM_MAX_PACK_SIZE];
#else
static uint8_t pack_data[TM_MAX_PACK_SIZE] __attribute__((aligned(4)));
#endif
static uint32_t pack_size;
static uint32_t pack_received;
static uint32_t pack_transfer_crc;
static uint16_t pack_transaction;
static bool pack_ready;

static tm_metric_entry_t metrics[TM_MAX_CHANNELS];
static uint32_t metric_updated[TM_MAX_CHANNELS];
static uint8_t current_page;
static bool system_visible;
static uint32_t button_event_id;
static uint8_t tx_sequence;
static uint8_t tx_queue[4][TM_PACKET_SIZE];
static uint8_t tx_head;
static uint8_t tx_tail;
static bool reboot_pending;
static uint32_t reboot_at;

static bool range_valid(uint32_t offset, uint32_t count, uint32_t item_size,
                        uint32_t total) {
  if (offset > total || item_size == 0) return false;
  if (count > (total - offset) / item_size) return false;
  return true;
}

static bool string_valid(uint32_t offset, uint32_t total) {
  if (offset >= total) return false;
  for (uint32_t i = offset; i < total; i++) {
    if (pack_data[i] == 0) return true;
  }
  return false;
}

static bool pack_validate(void) {
  if (pack_size < sizeof(tm_pack_header_t)) return false;
  tm_pack_header_t *header = (tm_pack_header_t *)pack_data;
  if (memcmp(header->magic, "TMPK", 4) != 0 ||
      header->version != TM_PACK_VERSION ||
      header->header_size != sizeof(tm_pack_header_t) ||
      header->total_size != pack_size) {
    return false;
  }
  if (header->screen_count == 0 || header->screen_count > TM_MAX_SCREENS ||
      header->channel_count > TM_MAX_CHANNELS ||
      header->font_count > TM_MAX_FONTS ||
      header->image_count > TM_MAX_IMAGES ||
      header->animation_count > TM_MAX_ANIMATIONS ||
      header->widget_count > TM_MAX_SCREENS * TM_MAX_WIDGETS_PER_SCREEN) {
    return false;
  }
  uint32_t saved_crc = header->crc32;
  header->crc32 = 0;
  uint32_t actual_crc = tm_crc32(pack_data, pack_size);
  header->crc32 = saved_crc;
  if (actual_crc != saved_crc) return false;
  if (!range_valid(header->screens_offset, header->screen_count,
                   sizeof(tm_screen_t), pack_size) ||
      !range_valid(header->widgets_offset, header->widget_count,
                   sizeof(tm_widget_t), pack_size) ||
      !range_valid(header->fonts_offset, header->font_count, sizeof(tm_font_t),
                   pack_size) ||
      !range_valid(header->images_offset, header->image_count,
                   sizeof(tm_image_t), pack_size) ||
      !range_valid(header->animations_offset, header->animation_count,
                   sizeof(tm_animation_t), pack_size) ||
      !range_valid(header->resources_offset, header->resources_size, 1,
                   pack_size)) {
    return false;
  }

  const tm_screen_t *screens =
      (const tm_screen_t *)(pack_data + header->screens_offset);
  for (uint8_t i = 0; i < header->screen_count; i++) {
    if (screens[i].widget_count > TM_MAX_WIDGETS_PER_SCREEN ||
        screens[i].first_widget > header->widget_count ||
        screens[i].widget_count >
            header->widget_count - screens[i].first_widget ||
        !string_valid(screens[i].name_offset, pack_size)) {
      return false;
    }
  }

  const tm_widget_t *widgets =
      (const tm_widget_t *)(pack_data + header->widgets_offset);
  for (uint16_t i = 0; i < header->widget_count; i++) {
    const tm_widget_t *widget = &widgets[i];
    if (widget->type < TM_WIDGET_STATIC_TEXT ||
        widget->type > TM_WIDGET_WARNING || widget->x >= 128 ||
        widget->y >= 64 || widget->width == 0 || widget->height == 0 ||
        widget->channel_id >= TM_MAX_CHANNELS) {
      return false;
    }
    if (widget->resource_offset != 0 &&
        widget->resource_offset >= pack_size) {
      return false;
    }
    if (widget->font_id != 0xff && widget->font_id >= header->font_count)
      return false;
  }

  const tm_font_t *fonts =
      (const tm_font_t *)(pack_data + header->fonts_offset);
  for (uint8_t i = 0; i < header->font_count; i++) {
    if (!range_valid(fonts[i].glyphs_offset, fonts[i].glyph_count,
                     sizeof(tm_glyph_t), pack_size))
      return false;
    const tm_glyph_t *glyphs =
        (const tm_glyph_t *)(pack_data + fonts[i].glyphs_offset);
    for (uint16_t g = 0; g < fonts[i].glyph_count; g++) {
      if (!range_valid(glyphs[g].data_offset, glyphs[g].data_size, 1,
                       pack_size))
        return false;
    }
  }

  const tm_image_t *images =
      (const tm_image_t *)(pack_data + header->images_offset);
  for (uint8_t i = 0; i < header->image_count; i++) {
    if (images[i].width == 0 || images[i].height == 0 ||
        !range_valid(images[i].data_offset, images[i].data_size, 1,
                     pack_size))
      return false;
  }

  const tm_animation_t *animations =
      (const tm_animation_t *)(pack_data + header->animations_offset);
  for (uint8_t i = 0; i < header->animation_count; i++) {
    if (animations[i].frame_count == 0 || animations[i].fps == 0 ||
        animations[i].fps > 12 ||
        !range_valid(animations[i].frames_offset, animations[i].frame_count,
                     sizeof(tm_animation_frame_t), pack_size))
      return false;
    const tm_animation_frame_t *frames = (const tm_animation_frame_t *)(
        pack_data + animations[i].frames_offset);
    for (uint8_t f = 0; f < animations[i].frame_count; f++) {
      if (!range_valid(frames[f].data_offset, frames[f].data_size, 1,
                       pack_size))
        return false;
    }
  }
  return true;
}

static void packet_finalize(tm_packet_t *packet) {
  packet->magic[0] = 'T';
  packet->magic[1] = 'M';
  packet->version = TM_PROTOCOL_VERSION;
  packet->crc32 = tm_crc32((const uint8_t *)packet, 60);
}

static tm_packet_t *queue_packet(uint8_t type, uint8_t sequence,
                                 uint16_t payload_length) {
  uint8_t next = (uint8_t)((tx_head + 1) % 4);
  if (next == tx_tail) tx_tail = (uint8_t)((tx_tail + 1) % 4);
  tm_packet_t *packet = (tm_packet_t *)tx_queue[tx_head];
  memset(packet, 0, sizeof(*packet));
  packet->type = type;
  packet->sequence = sequence;
  packet->payload_length = payload_length;
  tx_head = next;
  return packet;
}

static void ack(uint8_t sequence, uint8_t status, uint32_t expected_offset) {
  tm_packet_t *packet = queue_packet(status == TM_ERROR_NONE ? TM_MSG_ACK
                                                             : TM_MSG_NACK,
                                     sequence, 6);
  packet->payload[0] = status;
  packet->payload[1] = 0;
  memcpy(packet->payload + 2, &expected_offset, sizeof(expected_offset));
  packet_finalize(packet);
}

static void capabilities(uint8_t sequence) {
  tm_packet_t *packet = queue_packet(TM_MSG_CAPABILITIES, sequence, 16);
  packet->payload[0] = 1;
  packet->payload[1] = 0;
  packet->payload[2] = 0;
  packet->payload[3] = 128;
  packet->payload[4] = 64;
  packet->payload[5] = TM_MAX_SCREENS;
  packet->payload[6] = TM_MAX_WIDGETS_PER_SCREEN;
  packet->payload[7] = TM_MAX_CHANNELS;
  uint32_t max_pack = TM_MAX_PACK_SIZE;
  memcpy(packet->payload + 8, &max_pack, sizeof(max_pack));
  packet->payload[12] = pack_ready ? 1 : 0;
  packet->payload[13] = current_page;
  packet_finalize(packet);
}

void monitor_init(void) {
  memset(pack_data, 0, sizeof(pack_data));
  memset(metrics, 0, sizeof(metrics));
  memset(metric_updated, 0, sizeof(metric_updated));
  for (uint16_t i = 0; i < TM_MAX_CHANNELS; i++) {
    metrics[i].channel_id = i;
    metrics[i].status = TM_STATUS_UNAVAILABLE;
  }
  pack_size = 0;
  pack_received = 0;
  pack_ready = false;
  current_page = 0;
  system_visible = false;
  button_event_id = 0;
  tx_sequence = 0;
  tx_head = tx_tail = 0;
  reboot_pending = false;
  renderer_init();
  renderer_waiting();
}

bool monitor_handle_packet(const uint8_t input[TM_PACKET_SIZE]) {
  const tm_packet_t *packet = (const tm_packet_t *)input;
  if (packet->magic[0] != 'T' || packet->magic[1] != 'M') return false;
  if (packet->payload_length > TM_PACKET_PAYLOAD_SIZE) {
    ack(packet->sequence, TM_ERROR_BAD_LENGTH, pack_received);
    return true;
  }
  if (tm_crc32(input, 60) != packet->crc32) {
    ack(packet->sequence, TM_ERROR_BAD_CRC, pack_received);
    return true;
  }
  if (packet->version != TM_PROTOCOL_VERSION) {
    ack(packet->sequence, TM_ERROR_BAD_VERSION, pack_received);
    return true;
  }

  switch (packet->type) {
    case TM_MSG_HELLO:
      capabilities(packet->sequence);
      break;
    case TM_MSG_PING: {
      tm_packet_t *response =
          queue_packet(TM_MSG_PONG, packet->sequence, packet->payload_length);
      memcpy(response->payload, packet->payload, packet->payload_length);
      packet_finalize(response);
      break;
    }
    case TM_MSG_PACK_BEGIN: {
      if (packet->payload_length < 10) {
        ack(packet->sequence, TM_ERROR_BAD_LENGTH, 0);
        break;
      }
      memcpy(&pack_transaction, packet->payload, 2);
      memcpy(&pack_size, packet->payload + 2, 4);
      memcpy(&pack_transfer_crc, packet->payload + 6, 4);
      if (pack_size == 0 || pack_size > TM_MAX_PACK_SIZE) {
        pack_size = 0;
        ack(packet->sequence, TM_ERROR_PACK_TOO_LARGE, 0);
        break;
      }
      pack_received = 0;
      pack_ready = false;
      current_page = 0;
      renderer_waiting();
      ack(packet->sequence, TM_ERROR_NONE, 0);
      break;
    }
    case TM_MSG_PACK_DATA: {
      if (packet->payload_length < 6 || pack_size == 0) {
        ack(packet->sequence, TM_ERROR_BAD_STATE, pack_received);
        break;
      }
      uint16_t transaction;
      uint32_t offset;
      memcpy(&transaction, packet->payload, 2);
      memcpy(&offset, packet->payload + 2, 4);
      uint32_t data_size = packet->payload_length - 6;
      if (transaction != pack_transaction || offset != pack_received ||
          data_size > pack_size - pack_received) {
        ack(packet->sequence, TM_ERROR_BAD_OFFSET, pack_received);
        break;
      }
      memcpy(pack_data + pack_received, packet->payload + 6, data_size);
      pack_received += data_size;
      ack(packet->sequence, TM_ERROR_NONE, pack_received);
      break;
    }
    case TM_MSG_PACK_COMMIT: {
      uint16_t transaction = 0;
      if (packet->payload_length >= 2)
        memcpy(&transaction, packet->payload, 2);
      if (transaction != pack_transaction || pack_received != pack_size) {
        ack(packet->sequence, TM_ERROR_BAD_STATE, pack_received);
        break;
      }
      if (tm_crc32(pack_data, pack_size) != pack_transfer_crc ||
          !pack_validate()) {
        ack(packet->sequence, TM_ERROR_PACK_INVALID, pack_received);
        break;
      }
      pack_ready = true;
      current_page = 0;
      system_visible = false;
      renderer_invalidate();
      ack(packet->sequence, TM_ERROR_NONE, pack_received);
      break;
    }
    case TM_MSG_METRICS: {
      if (!pack_ready ||
          packet->payload_length % sizeof(tm_metric_entry_t) != 0) {
        ack(packet->sequence, TM_ERROR_BAD_LENGTH, 0);
        break;
      }
      uint32_t now = svc_timer_ms();
      uint16_t count = packet->payload_length / sizeof(tm_metric_entry_t);
      for (uint16_t i = 0; i < count; i++) {
        tm_metric_entry_t entry;
        memcpy(&entry, packet->payload + i * sizeof(entry), sizeof(entry));
        if (entry.channel_id < TM_MAX_CHANNELS) {
          metrics[entry.channel_id] = entry;
          metric_updated[entry.channel_id] = now;
        }
      }
      renderer_invalidate();
      ack(packet->sequence, TM_ERROR_NONE, 0);
      break;
    }
    case TM_MSG_SET_PAGE:
      if (pack_ready && packet->payload_length >= 1 &&
          packet->payload[0] < monitor_pack_header()->screen_count) {
        current_page = packet->payload[0];
        system_visible = false;
        renderer_invalidate();
        ack(packet->sequence, TM_ERROR_NONE, 0);
      } else {
        ack(packet->sequence, TM_ERROR_BAD_LENGTH, 0);
      }
      break;
    case TM_MSG_REBOOT_BOOTLOADER:
      ack(packet->sequence, TM_ERROR_NONE, 0);
      reboot_pending = true;
      reboot_at = svc_timer_ms() + 150;
      break;
    default:
      ack(packet->sequence, TM_ERROR_UNSUPPORTED, 0);
      break;
  }
  return true;
}

static void execute_action(const tm_action_t *action, uint8_t button,
                           uint8_t gesture) {
  if (!pack_ready || action->type == TM_ACTION_NONE) return;
  const tm_pack_header_t *header = monitor_pack_header();
  if (action->type == TM_ACTION_NEXT_PAGE) {
    current_page = (uint8_t)((current_page + 1) % header->screen_count);
    renderer_invalidate();
  } else if (action->type == TM_ACTION_PREVIOUS_PAGE) {
    current_page = (uint8_t)((current_page + header->screen_count - 1) %
                             header->screen_count);
    renderer_invalidate();
  } else if (action->type == TM_ACTION_GO_TO_PAGE &&
             action->target < header->screen_count) {
    current_page = action->target;
    renderer_invalidate();
  } else if (action->type == TM_ACTION_HOST) {
    tm_packet_t *packet =
        queue_packet(TM_MSG_BUTTON_EVENT, tx_sequence++, sizeof(tm_button_event_t));
    tm_button_event_t event = {.event_id = ++button_event_id,
                               .action_id = action->id,
                               .page = current_page,
                               .button = button,
                               .gesture = gesture,
                               .reserved = {0, 0, 0}};
    memcpy(packet->payload, &event, sizeof(event));
    packet_finalize(packet);
  }
}

void monitor_button(uint8_t button, uint8_t gesture) {
  if (system_visible) {
    if (gesture == TM_GESTURE_SHORT && button == TM_BUTTON_LEFT) {
      monitor_leave_system();
    } else if (gesture == TM_GESTURE_LONG && button == TM_BUTTON_RIGHT) {
      reboot_pending = true;
      reboot_at = svc_timer_ms() + 150;
    }
    return;
  }
  if (!pack_ready) return;
  const tm_pack_header_t *header = monitor_pack_header();
  const tm_screen_t *screens =
      (const tm_screen_t *)(pack_data + header->screens_offset);
  const tm_screen_t *screen = &screens[current_page];
  const tm_action_t *action;
  if (button == TM_BUTTON_LEFT)
    action = gesture == TM_GESTURE_LONG ? &screen->left_long
                                        : &screen->left_short;
  else
    action = gesture == TM_GESTURE_LONG ? &screen->right_long
                                        : &screen->right_short;
  execute_action(action, button, gesture);
}

void monitor_show_system(void) {
  system_visible = true;
  renderer_system();
}

void monitor_leave_system(void) {
  system_visible = false;
  renderer_invalidate();
}

void monitor_tick(uint32_t now_ms) {
  if (reboot_pending && (int32_t)(now_ms - reboot_at) >= 0) {
    svc_reboot_to_bootloader();
  }
  for (uint16_t i = 0; i < TM_MAX_CHANNELS; i++) {
    if (metrics[i].status != TM_STATUS_UNAVAILABLE &&
        metric_updated[i] != 0 && now_ms - metric_updated[i] > 5000 &&
        (metrics[i].status & TM_STATUS_STALE) == 0) {
      metrics[i].status |= TM_STATUS_STALE;
      renderer_invalidate();
    }
  }
  renderer_tick(now_ms);
}

bool monitor_take_packet(uint8_t output[TM_PACKET_SIZE]) {
  if (tx_tail == tx_head) return false;
  memcpy(output, tx_queue[tx_tail], TM_PACKET_SIZE);
  tx_tail = (uint8_t)((tx_tail + 1) % 4);
  return true;
}

const uint8_t *monitor_pack(void) { return pack_data; }
const tm_pack_header_t *monitor_pack_header(void) {
  return (const tm_pack_header_t *)pack_data;
}
const tm_metric_entry_t *monitor_metric(uint16_t channel_id) {
  if (channel_id >= TM_MAX_CHANNELS) return NULL;
  return &metrics[channel_id];
}
uint32_t monitor_metric_updated(uint16_t channel_id) {
  return channel_id < TM_MAX_CHANNELS ? metric_updated[channel_id] : 0;
}
uint8_t monitor_current_page(void) { return current_page; }
bool monitor_pack_active(void) { return pack_ready; }
bool monitor_system_visible(void) { return system_visible; }
bool monitor_reboot_requested(void) { return reboot_pending; }
