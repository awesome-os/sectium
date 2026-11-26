#ifndef HOLO_DISPLAY_WEBGPU_H
#define HOLO_DISPLAY_WEBGPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* B8G_ABI_MAGIC_V3 ("BGV3" little-endian: 0x33564742) */
#define B8G_ABI_MAGIC_V3          0x33564742u
#define HOLO_WEBGPU_RING_VERSION  1u
#define HOLO_WEBGPU_CACHE_LINE    64u

/* Ring header flags */
#define HOLO_RING_FLAG_NONE        0u
#define HOLO_RING_FLAG_LIGHT_THEME (1u << 0)
#define HOLO_RING_FLAG_DIRTY       (1u << 1)
#define HOLO_RING_FLAG_WALLPAPER   (1u << 2)

/* Command opcodes */
typedef enum HoloCmdOpcode {
    HOLO_CMD_NONE          = 0u,
    HOLO_CMD_SURFACE_QUAD  = 1u,
    HOLO_CMD_THEME_PALETTE = 2u,
    HOLO_CMD_COMMIT_FRAME  = 3u,
    HOLO_CMD_CURSOR        = 4u
} HoloCmdOpcode;

/* Surface flags */
#define HOLO_SURFACE_FLAG_NONE     0u
#define HOLO_SURFACE_FLAG_ACTIVE   (1u << 0)
#define HOLO_SURFACE_FLAG_FLOATING (1u << 1)
#define HOLO_SURFACE_FLAG_BLUR     (1u << 2)

/* Return status codes */
#define HOLO_WEBGPU_OK             0
#define HOLO_WEBGPU_ERR_INVALID   -1
#define HOLO_WEBGPU_ERR_FULL      -2
#define HOLO_WEBGPU_ERR_EMPTY     -3
#define HOLO_WEBGPU_ERR_MAGIC     -4

/*
 * 64-byte aligned B8G_ARENA_V1 ring header.
 * Byte layout matches Uint32Array[0..15] in holo-display-webgpu.mjs.
 */
typedef struct HoloWebGpuRingHeader {
    _Alignas(64) uint32_t magic; /* offset  0: B8G_ABI_MAGIC_V3 (0x33564742) */
    uint32_t version;            /* offset  4: 1 */
    uint32_t capacity;           /* offset  8: power-of-two slot count */
    uint32_t head;               /* offset 12: monotonic producer index */
    uint32_t tail;               /* offset 16: monotonic consumer index */
    uint32_t frame_seq;          /* offset 20: monotonic committed frame seq */
    uint32_t theme_accent_rgba;  /* offset 24: primary accent 0xRRGGBBAA */
    uint32_t theme_bg_rgba;      /* offset 28: backdrop bg 0xRRGGBBAA */
    uint32_t flags;              /* offset 32: HOLO_RING_FLAG_* bitmask */
    uint32_t theme_accent2_rgba; /* offset 36: secondary aurora accent 0xRRGGBBAA */
    uint32_t dropped_cmds;       /* offset 40: ring-full drop counter */
    uint32_t reserved0;          /* offset 44 */
    uint32_t reserved1;          /* offset 48 */
    uint32_t reserved2;          /* offset 52 */
    uint32_t reserved3;          /* offset 56 */
    uint32_t reserved4;          /* offset 60 */
} HoloWebGpuRingHeader;

/*
 * 64-byte aligned surface/composition command slot.
 * Byte layout matches Uint32Array/Float32Array stride 16 in holo-display-webgpu.mjs.
 */
typedef struct HoloSurfaceCmd {
    _Alignas(64) uint32_t opcode; /* offset  0: HoloCmdOpcode */
    uint32_t surface_id;    /* offset  4: surface ID (or frame_seq on COMMIT) */
    float    x;             /* offset  8: surface X (px) */
    float    y;             /* offset 12: surface Y (px) */
    float    w;             /* offset 16: surface width (px) */
    float    h;             /* offset 20: surface height (px) */
    float    radius;        /* offset 24: corner radius (px) */
    float    border_px;     /* offset 28: border thickness (px) */
    uint32_t bg_rgba;       /* offset 32: fill color 0xRRGGBBAA */
    uint32_t border_rgba;   /* offset 36: border/accent color 0xRRGGBBAA */
    uint32_t flags;         /* offset 40: HOLO_SURFACE_FLAG_* bitmask */
    uint32_t accent2_rgba;  /* offset 44: secondary accent / glow tint 0xRRGGBBAA */
    float    z_index;       /* offset 48: stacking order */
    float    opacity;       /* offset 52: [0.0 .. 1.0] */
    uint32_t reserved0;     /* offset 56 */
    uint32_t reserved1;     /* offset 60 */
} HoloSurfaceCmd;

_Static_assert(sizeof(HoloWebGpuRingHeader) == 64u, "HoloWebGpuRingHeader must be exactly 64 bytes");
_Static_assert(_Alignof(HoloWebGpuRingHeader) == 64u, "HoloWebGpuRingHeader must be 64-byte aligned");
_Static_assert(sizeof(HoloSurfaceCmd) == 64u, "HoloSurfaceCmd must be exactly 64 bytes");
_Static_assert(_Alignof(HoloSurfaceCmd) == 64u, "HoloSurfaceCmd must be 64-byte aligned");

/* Pack 8-bit channels into 0xRRGGBBAA */
static inline uint32_t holo_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

/* Compute total arena byte size for a ring with `capacity` slots */
static inline size_t holo_webgpu_arena_bytes(uint32_t capacity) {
    return sizeof(HoloWebGpuRingHeader) + ((size_t)capacity * sizeof(HoloSurfaceCmd));
}

/* Initialize a 64-byte aligned ring header and zero its `capacity` (power-of-two) command slots. */
int holo_webgpu_ring_init(HoloWebGpuRingHeader *hdr,
                          HoloSurfaceCmd *slots,
                          uint32_t capacity);

/* Submit a surface quad command (forces opcode = HOLO_CMD_SURFACE_QUAD). */
int holo_webgpu_submit_surface(HoloWebGpuRingHeader *hdr,
                               HoloSurfaceCmd *slots,
                               const HoloSurfaceCmd *cmd);

/* Update header theme palette and enqueue a HOLO_CMD_THEME_PALETTE command. */
int holo_webgpu_submit_theme(HoloWebGpuRingHeader *hdr,
                             HoloSurfaceCmd *slots,
                             uint32_t bg_rgba,
                             uint32_t accent_rgba,
                             uint32_t accent2_rgba,
                             uint32_t ring_flags);

/* Increment frame_seq and enqueue a HOLO_CMD_COMMIT_FRAME command. */
int holo_webgpu_commit_frame(HoloWebGpuRingHeader *hdr,
                             HoloSurfaceCmd *slots,
                             uint32_t *out_frame_seq);

/* Generic command submission (e.g., HOLO_CMD_CURSOR or custom pre-populated HoloSurfaceCmd). */
int holo_webgpu_submit_cmd(HoloWebGpuRingHeader *hdr,
                           HoloSurfaceCmd *slots,
                           const HoloSurfaceCmd *cmd);

/* Pop the next command from the ring into `*out_cmd`. Returns HOLO_WEBGPU_ERR_EMPTY if empty. */
int holo_webgpu_pop_cmd(HoloWebGpuRingHeader *hdr,
                        const HoloSurfaceCmd *slots,
                        HoloSurfaceCmd *out_cmd);

/* Number of unread commands currently in the ring. */
uint32_t holo_webgpu_ring_count(const HoloWebGpuRingHeader *hdr);

#ifdef __cplusplus
}
#endif

#endif /* HOLO_DISPLAY_WEBGPU_H */
