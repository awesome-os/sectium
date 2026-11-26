#include "holo_device_host.h"
#include <string.h>

static bool is_pow2(uint32_t v) {
  return v != 0u && (v & (v - 1u)) == 0u;
}

bool holo_hid_ring_init(HoloHidRing *ring, void *arena, size_t arena_bytes, uint32_t capacity,
                        uint32_t width, uint32_t height, uint32_t flags) {
  if (!ring || !arena || !is_pow2(capacity)) return false;
  size_t needed = sizeof(HoloHidRingHeader) + (size_t)capacity * sizeof(HoloHidEvent);
  if (arena_bytes < needed) return false;
  if (((uintptr_t)arena & 63u) != 0u) return false;

  memset(arena, 0, needed);
  HoloHidRingHeader *hdr = (HoloHidRingHeader *)arena;
  hdr->magic = HOLO_DEVICE_ABI_MAGIC;
  hdr->version = HOLO_DEVICE_ABI_VERSION;
  hdr->capacity = capacity;
  hdr->head = 0u;
  hdr->tail = 0u;
  hdr->dropped_events = 0u;
  hdr->active_mods = 0u;
  hdr->device_flags = flags;
  hdr->monitor_width = width;
  hdr->monitor_height = height;
  hdr->monitor_dpi_x100 = 100u;

  ring->hdr = hdr;
  ring->slots = (HoloHidEvent *)((uint8_t *)arena + sizeof(HoloHidRingHeader));
  return true;
}

uint32_t holo_hid_count(const HoloHidRing *ring) {
  if (!ring || !ring->hdr) return 0u;
  return ring->hdr->head - ring->hdr->tail;
}

bool holo_hid_push(HoloHidRing *ring, const HoloHidEvent *ev) {
  if (!ring || !ring->hdr || !ring->slots || !ev) return false;
  HoloHidRingHeader *hdr = ring->hdr;
  if (hdr->magic != HOLO_DEVICE_ABI_MAGIC) return false;

  uint32_t used = hdr->head - hdr->tail;
  if (used >= hdr->capacity) {
    hdr->dropped_events++;
    return false;
  }

  uint32_t idx = hdr->head & (hdr->capacity - 1u);
  ring->slots[idx] = *ev;
  ring->slots[idx].seq = hdr->head + 1u;
  hdr->active_mods = ev->modifiers;
  if (ev->type == HOLO_HID_SUPER_LOCK) {
    if (ev->code != 0u) hdr->device_flags |= HOLO_DEV_SUPER_LOCKED;
    else hdr->device_flags &= ~HOLO_DEV_SUPER_LOCKED;
  }
  hdr->head++;
  return true;
}

bool holo_hid_pop(HoloHidRing *ring, HoloHidEvent *out) {
  if (!ring || !ring->hdr || !ring->slots || !out) return false;
  HoloHidRingHeader *hdr = ring->hdr;
  if (hdr->magic != HOLO_DEVICE_ABI_MAGIC) return false;
  if (hdr->tail == hdr->head) return false;

  uint32_t idx = hdr->tail & (hdr->capacity - 1u);
  *out = ring->slots[idx];
  hdr->tail++;
  return true;
}
