#include "holo_device_host.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

int main(void) {
  _Alignas(64) uint8_t arena[64 + 8 * 64];
  HoloHidRing ring;
  CHECK(holo_hid_ring_init(&ring, arena, sizeof(arena), 8, 1920, 1080,
                           HOLO_DEV_FRAMELESS | HOLO_DEV_WEBGPU_ACTIVE | HOLO_DEV_LOCAL_V11));
  CHECK(ring.hdr->magic == HOLO_DEVICE_ABI_MAGIC);
  CHECK(ring.hdr->monitor_width == 1920 && ring.hdr->monitor_height == 1080);

  HoloHidEvent ev = {0};
  ev.type = HOLO_HID_KEY_DOWN;
  ev.code = 0x2Cu; /* Space */
  ev.modifiers = HOLO_MOD_SUPER | HOLO_MOD_CTRL | HOLO_MOD_SHIFT;
  ev.timestamp_us = 1000u;
  CHECK(holo_hid_push(&ring, &ev));
  CHECK(holo_hid_count(&ring) == 1u);
  CHECK(ring.hdr->active_mods == (HOLO_MOD_SUPER | HOLO_MOD_CTRL | HOLO_MOD_SHIFT));

  HoloHidEvent lockEv = {0};
  lockEv.type = HOLO_HID_SUPER_LOCK;
  lockEv.code = 1u;
  CHECK(holo_hid_push(&ring, &lockEv));
  CHECK((ring.hdr->device_flags & HOLO_DEV_SUPER_LOCKED) != 0u);

  HoloHidEvent out = {0};
  CHECK(holo_hid_pop(&ring, &out));
  CHECK(out.type == HOLO_HID_KEY_DOWN && out.code == 0x2Cu && out.seq == 1u);
  CHECK(holo_hid_pop(&ring, &out));
  CHECK(out.type == HOLO_HID_SUPER_LOCK && out.seq == 2u);
  CHECK(holo_hid_count(&ring) == 0u);

  /* Wraparound + backpressure check */
  for (uint32_t i = 0; i < 8; i++) {
    ev.code = i + 1u;
    CHECK(holo_hid_push(&ring, &ev));
  }
  CHECK(!holo_hid_push(&ring, &ev));
  CHECK(ring.hdr->dropped_events == 1u);

  printf("holo_device_host_test: PASS (B8G_ABI_MAGIC_V3=0x%08X, 64B aligned HID+Monitor+v11 boundary)\n",
         ring.hdr->magic);
  return 0;
}
