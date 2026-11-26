#pragma once

#include "holo_device_host.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOLO_KVM_MAX_DEVICES     8u
#define HOLO_KVM_MAX_SHARED_LIBS 32u

typedef enum HoloSharedLibFlags {
  HOLO_LIB_PURE_READONLY = 1u << 0, /* Mapped once in physical RAM (mmap PROT_READ / shared WASM Module) */
  HOLO_LIB_GPU_DRIVER    = 1u << 1, /* Shared Dawn/WebGPU pipeline / shader cache */
  HOLO_LIB_FONT_ATLAS    = 1u << 2, /* Shared glyph & icon atlas */
  HOLO_LIB_V11_VERIFIED  = 1u << 3  /* Verified against v11 SHA-512 digest */
} HoloSharedLibFlags;

/* Immutable Pure-Library RAM Descriptor shared across all devices (0 RAM duplication) */
typedef struct HoloSharedLibEntry {
  _Alignas(64) uint8_t sha512_prefix[16]; /* First 128 bits of v11 SHA-512 */
  uint64_t mapped_base_addr;              /* Shared read-only physical/virtual mapping */
  uint32_t byte_size;
  uint32_t flags;                         /* HoloSharedLibFlags */
  uint32_t attached_device_mask;          /* Bitmask of devices sharing this single RAM copy */
  uint32_t ref_count;
  char name[16];
} HoloSharedLibEntry;

/* Per-device strictly isolated KVM I/O endpoints */
typedef struct HoloKvmDeviceSlot {
  uint32_t device_id;
  bool online;
  char name[24];
  HoloHidRing hid_ring;        /* Private HID input ring (only active_hid_dev receives events!) */
  uint32_t display_surface_id; /* Private virtual monitor output channel */
  uint32_t shared_lib_mask;    /* Bitmask of attached read-only pure libs */
  uint64_t private_ram_bytes;  /* Per-device mutable heap/state only */
} HoloKvmDeviceSlot;

/* Virtual KVM Switch Matrix + Shared Pure-Lib Registry */
typedef struct HoloKvmSwitch {
  uint32_t magic;              /* HOLO_DEVICE_ABI_MAGIC (0x33564742) */
  uint32_t active_monitor_dev; /* Which device owns the primary physical monitor output */
  uint32_t active_hid_dev;     /* Which device owns physical keyboard/mouse input (can split!) */
  uint32_t switch_count;
  uint32_t lib_count;
  HoloKvmDeviceSlot devices[HOLO_KVM_MAX_DEVICES];
  HoloSharedLibEntry shared_libs[HOLO_KVM_MAX_SHARED_LIBS];
} HoloKvmSwitch;

_Static_assert(sizeof(HoloSharedLibEntry) == 64, "HoloSharedLibEntry must be 64 bytes");

void holo_kvm_init(HoloKvmSwitch *kvm);
bool holo_kvm_register_device(HoloKvmSwitch *kvm, uint32_t device_id, const char *name,
                              void *hid_arena, size_t hid_arena_bytes, uint32_t hid_cap,
                              uint32_t width, uint32_t height, uint64_t private_ram_bytes);

/* KVM I/O Switching (atomic monitor + HID focus, or split monitor vs. HID routing) */
bool holo_kvm_switch_both(HoloKvmSwitch *kvm, uint32_t device_id);
bool holo_kvm_switch_monitor(HoloKvmSwitch *kvm, uint32_t device_id);
bool holo_kvm_switch_hid(HoloKvmSwitch *kvm, uint32_t device_id);

/* Strict HID routing: ONLY active_hid_dev receives physical input events */
bool holo_kvm_route_hid(HoloKvmSwitch *kvm, const HoloHidEvent *ev);

/* Shared Pure-Lib RAM deduplication (register once by SHA-512, attach to N devices at 0 extra RAM) */
int32_t holo_kvm_register_shared_lib(HoloKvmSwitch *kvm, const char *name,
                                     const uint8_t sha512_prefix[16], uint64_t mapped_base_addr,
                                     uint32_t byte_size, uint32_t flags);
bool holo_kvm_attach_shared_lib(HoloKvmSwitch *kvm, uint32_t device_id, uint32_t lib_idx);
uint64_t holo_kvm_total_shared_ram_bytes(const HoloKvmSwitch *kvm);
uint64_t holo_kvm_saved_ram_bytes(const HoloKvmSwitch *kvm);

#ifdef __cplusplus
}
#endif
