#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOLO_DEVICE_ABI_MAGIC 0x33564742u /* B8G_ABI_MAGIC_V3 */
#define HOLO_DEVICE_ABI_VERSION 1u

typedef enum HoloHidEventType {
  HOLO_HID_NONE = 0,
  HOLO_HID_KEY_DOWN = 1,
  HOLO_HID_KEY_UP = 2,
  HOLO_HID_POINTER_MOVE = 3,
  HOLO_HID_POINTER_DOWN = 4,
  HOLO_HID_POINTER_UP = 5,
  HOLO_HID_WHEEL = 6,
  HOLO_HID_FOCUS = 7,
  HOLO_HID_SUPER_LOCK = 8
} HoloHidEventType;

typedef enum HoloHidModFlags {
  HOLO_MOD_SHIFT = 1u << 0,
  HOLO_MOD_CTRL  = 1u << 1,
  HOLO_MOD_ALT   = 1u << 2,
  HOLO_MOD_SUPER = 1u << 3
} HoloHidModFlags;

typedef enum HoloDeviceFlags {
  HOLO_DEV_FRAMELESS     = 1u << 0,
  HOLO_DEV_SUPER_LOCKED  = 1u << 1,
  HOLO_DEV_WEBGPU_ACTIVE = 1u << 2,
  HOLO_DEV_OPFS_V11      = 1u << 3,
  HOLO_DEV_LOCAL_V11     = 1u << 4
} HoloDeviceFlags;

/* 64-byte aligned HID ring header in B8G_ARENA_V1 SharedArrayBuffer */
typedef struct HoloHidRingHeader {
  _Alignas(64) uint32_t magic;
  uint32_t version;
  uint32_t capacity;
  uint32_t head;
  uint32_t tail;
  uint32_t dropped_events;
  uint32_t active_mods;
  uint32_t device_flags;
  uint32_t monitor_width;
  uint32_t monitor_height;
  uint32_t monitor_dpi_x100;
  uint32_t reserved[5];
} HoloHidRingHeader;

/* 64-byte aligned HID event slot */
typedef struct HoloHidEvent {
  _Alignas(64) uint32_t type;
  uint32_t code;         /* USB HID usage / DOM key code hash */
  uint32_t modifiers;    /* HoloHidModFlags */
  uint32_t button;       /* Mouse button mask */
  float x;               /* Normalized or pixel X */
  float y;               /* Normalized or pixel Y */
  float dx;              /* Wheel / delta X */
  float dy;              /* Wheel / delta Y */
  uint64_t timestamp_us;
  uint32_t seq;
  uint32_t reserved[5];
} HoloHidEvent;

_Static_assert(sizeof(HoloHidRingHeader) == 64, "HoloHidRingHeader must be 64 bytes");
_Static_assert(sizeof(HoloHidEvent) == 64, "HoloHidEvent must be 64 bytes");

typedef struct HoloHidRing {
  HoloHidRingHeader *hdr;
  HoloHidEvent *slots;
} HoloHidRing;

bool holo_hid_ring_init(HoloHidRing *ring, void *arena, size_t arena_bytes, uint32_t capacity,
                        uint32_t width, uint32_t height, uint32_t flags);
bool holo_hid_push(HoloHidRing *ring, const HoloHidEvent *ev);
bool holo_hid_pop(HoloHidRing *ring, HoloHidEvent *out);
uint32_t holo_hid_count(const HoloHidRing *ring);

#ifdef __cplusplus
}
#endif
