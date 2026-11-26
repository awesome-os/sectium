#include "holo_kvm_switch.h"
#include <string.h>

void holo_kvm_init(HoloKvmSwitch *kvm) {
  if (!kvm) return;
  memset(kvm, 0, sizeof(*kvm));
  kvm->magic = HOLO_DEVICE_ABI_MAGIC;
  kvm->active_monitor_dev = 0u;
  kvm->active_hid_dev = 0u;
}

bool holo_kvm_register_device(HoloKvmSwitch *kvm, uint32_t device_id, const char *name,
                              void *hid_arena, size_t hid_arena_bytes, uint32_t hid_cap,
                              uint32_t width, uint32_t height, uint64_t private_ram_bytes) {
  if (!kvm || kvm->magic != HOLO_DEVICE_ABI_MAGIC || device_id >= HOLO_KVM_MAX_DEVICES) return false;
  HoloKvmDeviceSlot *slot = &kvm->devices[device_id];
  memset(slot, 0, sizeof(*slot));

  if (!holo_hid_ring_init(&slot->hid_ring, hid_arena, hid_arena_bytes, hid_cap, width, height,
                          HOLO_DEV_FRAMELESS | HOLO_DEV_WEBGPU_ACTIVE | HOLO_DEV_LOCAL_V11)) {
    return false;
  }

  slot->device_id = device_id;
  slot->online = true;
  slot->display_surface_id = device_id + 1u;
  slot->private_ram_bytes = private_ram_bytes;
  if (name) {
    size_t len = strlen(name);
    if (len >= sizeof(slot->name)) len = sizeof(slot->name) - 1u;
    memcpy(slot->name, name, len);
    slot->name[len] = '\0';
  }
  return true;
}

bool holo_kvm_switch_monitor(HoloKvmSwitch *kvm, uint32_t device_id) {
  if (!kvm || device_id >= HOLO_KVM_MAX_DEVICES || !kvm->devices[device_id].online) return false;
  kvm->active_monitor_dev = device_id;
  kvm->switch_count++;
  return true;
}

bool holo_kvm_switch_hid(HoloKvmSwitch *kvm, uint32_t device_id) {
  if (!kvm || device_id >= HOLO_KVM_MAX_DEVICES || !kvm->devices[device_id].online) return false;
  kvm->active_hid_dev = device_id;
  kvm->switch_count++;
  return true;
}

bool holo_kvm_switch_both(HoloKvmSwitch *kvm, uint32_t device_id) {
  if (!kvm || device_id >= HOLO_KVM_MAX_DEVICES || !kvm->devices[device_id].online) return false;
  kvm->active_monitor_dev = device_id;
  kvm->active_hid_dev = device_id;
  kvm->switch_count++;
  return true;
}

bool holo_kvm_route_hid(HoloKvmSwitch *kvm, const HoloHidEvent *ev) {
  if (!kvm || !ev || kvm->active_hid_dev >= HOLO_KVM_MAX_DEVICES) return false;
  HoloKvmDeviceSlot *target = &kvm->devices[kvm->active_hid_dev];
  if (!target->online) return false;
  return holo_hid_push(&target->hid_ring, ev);
}

int32_t holo_kvm_register_shared_lib(HoloKvmSwitch *kvm, const char *name,
                                     const uint8_t sha512_prefix[16], uint64_t mapped_base_addr,
                                     uint32_t byte_size, uint32_t flags) {
  if (!kvm || !sha512_prefix || byte_size == 0u) return -1;
  /* Pure libs MUST be read-only to be safely shared across devices */
  if ((flags & HOLO_LIB_PURE_READONLY) == 0u) return -1;

  /* Deduplicate by SHA-512 prefix if already mapped in RAM */
  for (uint32_t i = 0; i < kvm->lib_count; i++) {
    if (memcmp(kvm->shared_libs[i].sha512_prefix, sha512_prefix, 16) == 0) {
      return (int32_t)i;
    }
  }
  if (kvm->lib_count >= HOLO_KVM_MAX_SHARED_LIBS) return -1;

  uint32_t idx = kvm->lib_count++;
  HoloSharedLibEntry *e = &kvm->shared_libs[idx];
  memset(e, 0, sizeof(*e));
  memcpy(e->sha512_prefix, sha512_prefix, 16);
  e->mapped_base_addr = mapped_base_addr;
  e->byte_size = byte_size;
  e->flags = flags;
  if (name) {
    size_t len = strlen(name);
    if (len >= sizeof(e->name)) len = sizeof(e->name) - 1u;
    memcpy(e->name, name, len);
    e->name[len] = '\0';
  }
  return (int32_t)idx;
}

bool holo_kvm_attach_shared_lib(HoloKvmSwitch *kvm, uint32_t device_id, uint32_t lib_idx) {
  if (!kvm || device_id >= HOLO_KVM_MAX_DEVICES || !kvm->devices[device_id].online) return false;
  if (lib_idx >= kvm->lib_count) return false;

  HoloSharedLibEntry *e = &kvm->shared_libs[lib_idx];
  uint32_t bit = 1u << device_id;
  if ((e->attached_device_mask & bit) == 0u) {
    e->attached_device_mask |= bit;
    e->ref_count++;
    kvm->devices[device_id].shared_lib_mask |= (1u << lib_idx);
  }
  return true;
}

uint64_t holo_kvm_total_shared_ram_bytes(const HoloKvmSwitch *kvm) {
  if (!kvm) return 0u;
  uint64_t total = 0u;
  for (uint32_t i = 0; i < kvm->lib_count; i++) {
    if (kvm->shared_libs[i].ref_count > 0u) {
      total += kvm->shared_libs[i].byte_size;
    }
  }
  return total;
}

uint64_t holo_kvm_saved_ram_bytes(const HoloKvmSwitch *kvm) {
  if (!kvm) return 0u;
  uint64_t saved = 0u;
  for (uint32_t i = 0; i < kvm->lib_count; i++) {
    if (kvm->shared_libs[i].ref_count > 1u) {
      saved += (uint64_t)(kvm->shared_libs[i].ref_count - 1u) * kvm->shared_libs[i].byte_size;
    }
  }
  return saved;
}
