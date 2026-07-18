#include <libopencm3/usb/hid.h>
#include <libopencm3/usb/usbd.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "buttons.h"
#include "monitor.h"
#include "oled.h"
#include "rng.h"
#include "setup.h"
#include "supervise.h"
#include "timer.h"

static const struct usb_device_descriptor device_descriptor = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x1209,
    .idProduct = 0x53c1,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t hid_report_descriptor[] = {
    0x06, 0x00, 0xff,  // Usage Page (Vendor)
    0x09, 0x01,        // Usage
    0xa1, 0x01,        // Collection (Application)
    0x15, 0x00,        // Logical minimum
    0x26, 0xff, 0x00,  // Logical maximum
    0x75, 0x08,        // Report size
    0x95, 0x40,        // Report count (64)
    0x09, 0x02,
    0x81, 0x02,  // Input
    0x95, 0x40,
    0x09, 0x03,
    0x91, 0x02,  // Output
    0xc0,
};

static const struct {
  struct usb_hid_descriptor hid_descriptor;
  struct {
    uint8_t type;
    uint16_t length;
  } __attribute__((packed)) report;
} __attribute__((packed)) hid_function = {
    .hid_descriptor =
        {
            .bLength = sizeof(hid_function),
            .bDescriptorType = USB_DT_HID,
            .bcdHID = 0x0111,
            .bCountryCode = 0,
            .bNumDescriptors = 1,
        },
    .report =
        {
            .type = USB_DT_REPORT,
            .length = sizeof(hid_report_descriptor),
        },
};

static const struct usb_endpoint_descriptor endpoints[] = {
    {.bLength = USB_DT_ENDPOINT_SIZE,
     .bDescriptorType = USB_DT_ENDPOINT,
     .bEndpointAddress = 0x81,
     .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
     .wMaxPacketSize = 64,
     .bInterval = 1},
    {.bLength = USB_DT_ENDPOINT_SIZE,
     .bDescriptorType = USB_DT_ENDPOINT,
     .bEndpointAddress = 0x02,
     .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
     .wMaxPacketSize = 64,
     .bInterval = 1},
};

static const struct usb_interface_descriptor interface_descriptor[] = {{
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_HID,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0,
    .iInterface = 0,
    .endpoint = endpoints,
    .extra = &hid_function,
    .extralen = sizeof(hid_function),
}};

static const struct usb_interface interfaces[] = {{
    .num_altsetting = 1,
    .altsetting = interface_descriptor,
}};

static const struct usb_config_descriptor config_descriptor = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0,
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0x80,
    .bMaxPower = 0x32,
    .interface = interfaces,
};

static const char *usb_strings[] = {
    "Trezor Community", "Trezor PC Monitor", "PCM00001"};

static usbd_device *usb_device;
static uint8_t control_buffer[128];

static enum usbd_request_return_codes control_request(
    usbd_device *device, struct usb_setup_data *request, uint8_t **buffer,
    uint16_t *length, usbd_control_complete_callback *complete) {
  (void)device;
  (void)complete;
  if (request->bmRequestType != 0x81 ||
      request->bRequest != USB_REQ_GET_DESCRIPTOR ||
      request->wValue != 0x2200)
    return USBD_REQ_NOTSUPP;
  *buffer = (uint8_t *)hid_report_descriptor;
  *length = sizeof(hid_report_descriptor);
  return USBD_REQ_HANDLED;
}

static void receive_packet(usbd_device *device, uint8_t endpoint) {
  (void)endpoint;
  uint8_t packet[TM_PACKET_SIZE] __attribute__((aligned(4)));
  if (usbd_ep_read_packet(device, 0x02, packet, sizeof(packet)) ==
      sizeof(packet)) {
    monitor_handle_packet(packet);
  }
}

static void set_config(usbd_device *device, uint16_t value) {
  (void)value;
  usbd_ep_setup(device, 0x81, USB_ENDPOINT_ATTR_INTERRUPT, 64, NULL);
  usbd_ep_setup(device, 0x02, USB_ENDPOINT_ATTR_INTERRUPT, 64, receive_packet);
  usbd_register_control_callback(
      device, USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
      USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT, control_request);
}

static void usb_init(void) {
  usb_device = usbd_init(&otgfs_usb_driver, &device_descriptor,
                         &config_descriptor, usb_strings, 3, control_buffer,
                         sizeof(control_buffer));
  usbd_register_set_config_callback(usb_device, set_config);
}

typedef struct {
  bool down;
  bool long_sent;
  uint32_t since;
} physical_button_t;

static physical_button_t left_button;
static physical_button_t right_button;
static bool chord_active;
static uint32_t chord_since;

static void update_one_button(physical_button_t *state, bool down,
                              bool other_down, uint8_t identity,
                              uint32_t now_ms) {
  if (down && !state->down) {
    state->down = true;
    state->long_sent = false;
    state->since = now_ms;
  } else if (!down && state->down) {
    uint32_t held = now_ms - state->since;
    if (!chord_active && !state->long_sent && held >= 30)
      monitor_button(identity, TM_GESTURE_SHORT);
    state->down = false;
    state->long_sent = false;
  } else if (down && !other_down && !state->long_sent &&
             now_ms - state->since >= 650) {
    state->long_sent = true;
    monitor_button(identity, TM_GESTURE_LONG);
  }
}

static void update_buttons(uint32_t now_ms) {
  uint16_t raw = buttonRead();
  bool left = (raw & BTN_PIN_NO) == 0;
  bool right = (raw & BTN_PIN_YES) == 0;
  if (left && right) {
    if (!chord_active && chord_since == 0) chord_since = now_ms;
    if (!chord_active && now_ms - chord_since >= 2000) {
      chord_active = true;
      monitor_show_system();
    }
  } else {
    if (!left && !right) chord_active = false;
    chord_since = 0;
  }
  update_one_button(&left_button, left, right, TM_BUTTON_LEFT, now_ms);
  update_one_button(&right_button, right, left, TM_BUTTON_RIGHT, now_ms);
}

int main(void) {
  setupApp();
  __stack_chk_guard = random32();
  timer_init();
  usb_init();
  monitor_init();

  uint8_t outgoing[TM_PACKET_SIZE] __attribute__((aligned(4)));
  bool outgoing_ready = false;
  for (;;) {
    usbd_poll(usb_device);
    uint32_t now = svc_timer_ms();
    update_buttons(now);
    monitor_tick(now);
    if (!outgoing_ready) outgoing_ready = monitor_take_packet(outgoing);
    if (outgoing_ready &&
        usbd_ep_write_packet(usb_device, 0x81, outgoing, sizeof(outgoing)) ==
            sizeof(outgoing)) {
      outgoing_ready = false;
      if (monitor_reboot_requested()) svc_reboot_to_bootloader();
    }
  }
}
