#include "holo_display_webgpu.h"

#include <string.h>

static int holo_is_power_of_two(uint32_t v) {
    return (v != 0u) && ((v & (v - 1u)) == 0u);
}

static int holo_validate_ring(const HoloWebGpuRingHeader *hdr, const HoloSurfaceCmd *slots) {
    if (hdr == NULL || slots == NULL) {
        return HOLO_WEBGPU_ERR_INVALID;
    }
    if (__atomic_load_n(&hdr->magic, __ATOMIC_ACQUIRE) != B8G_ABI_MAGIC_V3) {
        return HOLO_WEBGPU_ERR_MAGIC;
    }
    if (hdr->version != HOLO_WEBGPU_RING_VERSION || !holo_is_power_of_two(hdr->capacity)) {
        return HOLO_WEBGPU_ERR_INVALID;
    }
    return HOLO_WEBGPU_OK;
}

int holo_webgpu_ring_init(HoloWebGpuRingHeader *hdr,
                          HoloSurfaceCmd *slots,
                          uint32_t capacity) {
    if (hdr == NULL || slots == NULL || !holo_is_power_of_two(capacity)) {
        return HOLO_WEBGPU_ERR_INVALID;
    }

    memset(hdr, 0, sizeof(*hdr));
    memset(slots, 0, (size_t)capacity * sizeof(HoloSurfaceCmd));

    hdr->version            = HOLO_WEBGPU_RING_VERSION;
    hdr->capacity           = capacity;
    hdr->head               = 0u;
    hdr->tail               = 0u;
    hdr->frame_seq          = 0u;
    hdr->theme_accent_rgba  = holo_rgba(0x7Cu, 0x3Au, 0xEDu, 0xFFu); /* violet-600 */
    hdr->theme_bg_rgba      = holo_rgba(0x0Bu, 0x0Fu, 0x19u, 0xFFu); /* deep obsidian */
    hdr->theme_accent2_rgba = holo_rgba(0x06u, 0xB6u, 0xD4u, 0xFFu); /* cyan-500 */
    hdr->flags              = HOLO_RING_FLAG_NONE;
    hdr->dropped_cmds       = 0u;

    /* Publish magic with release semantics so concurrent readers see initialized header */
    __atomic_store_n(&hdr->magic, B8G_ABI_MAGIC_V3, __ATOMIC_RELEASE);
    return HOLO_WEBGPU_OK;
}

int holo_webgpu_submit_cmd(HoloWebGpuRingHeader *hdr,
                           HoloSurfaceCmd *slots,
                           const HoloSurfaceCmd *cmd) {
    int rc = holo_validate_ring(hdr, slots);
    if (rc != HOLO_WEBGPU_OK) {
        return rc;
    }
    if (cmd == NULL || cmd->opcode == HOLO_CMD_NONE) {
        return HOLO_WEBGPU_ERR_INVALID;
    }

    uint32_t head = __atomic_load_n(&hdr->head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&hdr->tail, __ATOMIC_ACQUIRE);
    uint32_t cap  = hdr->capacity;

    if ((uint32_t)(head - tail) >= cap) {
        __atomic_fetch_add(&hdr->dropped_cmds, 1u, __ATOMIC_RELAXED);
        return HOLO_WEBGPU_ERR_FULL;
    }

    uint32_t mask = cap - 1u;
    slots[head & mask] = *cmd;

    __atomic_store_n(&hdr->head, head + 1u, __ATOMIC_RELEASE);
    return HOLO_WEBGPU_OK;
}

int holo_webgpu_submit_surface(HoloWebGpuRingHeader *hdr,
                               HoloSurfaceCmd *slots,
                               const HoloSurfaceCmd *cmd) {
    if (cmd == NULL) {
        return HOLO_WEBGPU_ERR_INVALID;
    }

    HoloSurfaceCmd copy = *cmd;
    copy.opcode = HOLO_CMD_SURFACE_QUAD;
    if (copy.opacity <= 0.0f) {
        copy.opacity = 1.0f;
    }
    return holo_webgpu_submit_cmd(hdr, slots, &copy);
}

int holo_webgpu_submit_theme(HoloWebGpuRingHeader *hdr,
                             HoloSurfaceCmd *slots,
                             uint32_t bg_rgba,
                             uint32_t accent_rgba,
                             uint32_t accent2_rgba,
                             uint32_t ring_flags) {
    int rc = holo_validate_ring(hdr, slots);
    if (rc != HOLO_WEBGPU_OK) {
        return rc;
    }

    __atomic_store_n(&hdr->theme_bg_rgba, bg_rgba, __ATOMIC_RELAXED);
    __atomic_store_n(&hdr->theme_accent_rgba, accent_rgba, __ATOMIC_RELAXED);
    __atomic_store_n(&hdr->theme_accent2_rgba, accent2_rgba, __ATOMIC_RELAXED);
    __atomic_store_n(&hdr->flags, ring_flags | HOLO_RING_FLAG_DIRTY, __ATOMIC_RELAXED);

    HoloSurfaceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode       = HOLO_CMD_THEME_PALETTE;
    cmd.bg_rgba      = bg_rgba;
    cmd.border_rgba  = accent_rgba;
    cmd.accent2_rgba = accent2_rgba;
    cmd.flags        = ring_flags;
    cmd.opacity      = 1.0f;

    return holo_webgpu_submit_cmd(hdr, slots, &cmd);
}

int holo_webgpu_commit_frame(HoloWebGpuRingHeader *hdr,
                             HoloSurfaceCmd *slots,
                             uint32_t *out_frame_seq) {
    int rc = holo_validate_ring(hdr, slots);
    if (rc != HOLO_WEBGPU_OK) {
        return rc;
    }

    uint32_t next_seq = __atomic_load_n(&hdr->frame_seq, __ATOMIC_RELAXED) + 1u;

    HoloSurfaceCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode     = HOLO_CMD_COMMIT_FRAME;
    cmd.surface_id = next_seq;
    cmd.opacity    = 1.0f;

    rc = holo_webgpu_submit_cmd(hdr, slots, &cmd);
    if (rc != HOLO_WEBGPU_OK) {
        return rc;
    }

    __atomic_store_n(&hdr->frame_seq, next_seq, __ATOMIC_RELEASE);
    if (out_frame_seq != NULL) {
        *out_frame_seq = next_seq;
    }
    return HOLO_WEBGPU_OK;
}

int holo_webgpu_pop_cmd(HoloWebGpuRingHeader *hdr,
                        const HoloSurfaceCmd *slots,
                        HoloSurfaceCmd *out_cmd) {
    int rc = holo_validate_ring(hdr, slots);
    if (rc != HOLO_WEBGPU_OK) {
        return rc;
    }
    if (out_cmd == NULL) {
        return HOLO_WEBGPU_ERR_INVALID;
    }

    uint32_t tail = __atomic_load_n(&hdr->tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&hdr->head, __ATOMIC_ACQUIRE);

    if (tail == head) {
        return HOLO_WEBGPU_ERR_EMPTY;
    }

    uint32_t mask = hdr->capacity - 1u;
    *out_cmd = slots[tail & mask];

    __atomic_store_n(&hdr->tail, tail + 1u, __ATOMIC_RELEASE);
    return HOLO_WEBGPU_OK;
}

uint32_t holo_webgpu_ring_count(const HoloWebGpuRingHeader *hdr) {
    if (hdr == NULL || __atomic_load_n(&hdr->magic, __ATOMIC_ACQUIRE) != B8G_ABI_MAGIC_V3) {
        return 0u;
    }
    uint32_t head = __atomic_load_n(&hdr->head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&hdr->tail, __ATOMIC_ACQUIRE);
    return (uint32_t)(head - tail);
}
