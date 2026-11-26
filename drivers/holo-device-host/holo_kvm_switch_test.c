#include "holo_kvm_switch.h"
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
  _Alignas(64) uint8_t arena0[64 + 8 * 64];
  _Alignas(64) uint8_t arena1[64 + 8 * 64];
  _Alignas(64) uint8_t arena2[64 + 8 * 64];

  HoloKvmSwitch kvm;
  holo_kvm_init(&kvm);

  CHECK(holo_kvm_register_device(&kvm, 0, "sectium", arena0, sizeof(arena0), 8, 1920, 1080, 16u * 1024u * 1024u));
  CHECK(holo_kvm_register_device(&kvm, 1, "desktop-omarchy", arena1, sizeof(arena1), 8, 1920, 1080, 24u * 1024u * 1024u));
  CHECK(holo_kvm_register_device(&kvm, 2, "gitlab-holo", arena2, sizeof(arena2), 8, 1920, 1080, 12u * 1024u * 1024u));

  /* 1. Reject non-readonly libs from being shared across devices */
  uint8_t bad_hash[16] = {1, 2, 3};
  CHECK(holo_kvm_register_shared_lib(&kvm, "mutable-heap", bad_hash, 0x1000, 4096, 0u) == -1);

  /* 2. Register pure read-only shared drivers (Dawn WebGPU, Font Atlas, Coreutils WASM) */
  uint8_t h_gpu[16] = {0xAA, 0xBB, 0x01};
  uint8_t h_font[16] = {0xCC, 0xDD, 0x02};
  int32_t gpu_idx = holo_kvm_register_shared_lib(&kvm, "dawn-webgpu", h_gpu, 0x70000000, 32u * 1024u * 1024u,
                                                 HOLO_LIB_PURE_READONLY | HOLO_LIB_GPU_DRIVER | HOLO_LIB_V11_VERIFIED);
  int32_t font_idx = holo_kvm_register_shared_lib(&kvm, "font-atlas", h_font, 0x74000000, 8u * 1024u * 1024u,
                                                  HOLO_LIB_PURE_READONLY | HOLO_LIB_FONT_ATLAS | HOLO_LIB_V11_VERIFIED);
  CHECK(gpu_idx == 0 && font_idx == 1);
  /* Duplicate registration deduplicates to same slot */
  CHECK(holo_kvm_register_shared_lib(&kvm, "dawn-webgpu", h_gpu, 0x70000000, 32u * 1024u * 1024u,
                                     HOLO_LIB_PURE_READONLY) == gpu_idx);

  /* Attach both pure libs to all 3 devices -> 40 MB physical RAM instead of 120 MB (80 MB saved!) */
  for (uint32_t d = 0; d < 3; d++) {
    CHECK(holo_kvm_attach_shared_lib(&kvm, d, (uint32_t)gpu_idx));
    CHECK(holo_kvm_attach_shared_lib(&kvm, d, (uint32_t)font_idx));
  }
  CHECK(holo_kvm_total_shared_ram_bytes(&kvm) == 40u * 1024u * 1024u);
  CHECK(holo_kvm_saved_ram_bytes(&kvm) == 80u * 1024u * 1024u);

  /* 3. Strict KVM I/O Split: Switch both to Device 1 (Omarchy), route key -> ONLY Device 1 gets it */
  CHECK(holo_kvm_switch_both(&kvm, 1));
  HoloHidEvent key = {0};
  key.type = HOLO_HID_KEY_DOWN;
  key.code = 0x2Cu;
  key.modifiers = HOLO_MOD_SUPER;
  CHECK(holo_kvm_route_hid(&kvm, &key));
  CHECK(holo_hid_count(&kvm.devices[0].hid_ring) == 0u);
  CHECK(holo_hid_count(&kvm.devices[1].hid_ring) == 1u);
  CHECK(holo_hid_count(&kvm.devices[2].hid_ring) == 0u);

  /* 4. Split Monitor vs HID: Monitor watches Device 2 (GitLab), HID stays on Device 1 (Omarchy) */
  CHECK(holo_kvm_switch_monitor(&kvm, 2));
  CHECK(kvm.active_monitor_dev == 2u && kvm.active_hid_dev == 1u);
  CHECK(holo_kvm_route_hid(&kvm, &key));
  CHECK(holo_hid_count(&kvm.devices[1].hid_ring) == 2u);
  CHECK(holo_hid_count(&kvm.devices[2].hid_ring) == 0u);

  printf("holo_kvm_switch_test: PASS (3 devices, 40 MB shared pure-lib RAM, 80 MB saved, strict KVM HID/Monitor split)\n");
  return 0;
}
