#include "holo_display_webgpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks_run = 0;
static int g_checks_failed = 0;

#define CHECK(cond)                                                               \
    do {                                                                          \
        g_checks_run++;                                                           \
        if (!(cond)) {                                                            \
            g_checks_failed++;                                                    \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                         \
    } while (0)

/* Compile-time ABI verification */
_Static_assert(sizeof(HoloWebGpuRingHeader) == 64u, "Header size must be 64 bytes");
_Static_assert(_Alignof(HoloWebGpuRingHeader) == 64u, "Header alignment must be 64 bytes");
_Static_assert(sizeof(HoloSurfaceCmd) == 64u, "Cmd size must be 64 bytes");
_Static_assert(_Alignof(HoloSurfaceCmd) == 64u, "Cmd alignment must be 64 bytes");
_Static_assert(offsetof(HoloWebGpuRingHeader, magic) == 0u, "magic offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, version) == 4u, "version offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, capacity) == 8u, "capacity offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, head) == 12u, "head offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, tail) == 16u, "tail offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, frame_seq) == 20u, "frame_seq offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, theme_accent_rgba) == 24u, "theme_accent_rgba offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, theme_bg_rgba) == 28u, "theme_bg_rgba offset");
_Static_assert(offsetof(HoloWebGpuRingHeader, flags) == 32u, "flags offset");
_Static_assert(offsetof(HoloSurfaceCmd, opcode) == 0u, "opcode offset");
_Static_assert(offsetof(HoloSurfaceCmd, surface_id) == 4u, "surface_id offset");
_Static_assert(offsetof(HoloSurfaceCmd, x) == 8u, "x offset");
_Static_assert(offsetof(HoloSurfaceCmd, y) == 12u, "y offset");
_Static_assert(offsetof(HoloSurfaceCmd, w) == 16u, "w offset");
_Static_assert(offsetof(HoloSurfaceCmd, h) == 20u, "h offset");
_Static_assert(offsetof(HoloSurfaceCmd, radius) == 24u, "radius offset");
_Static_assert(offsetof(HoloSurfaceCmd, border_px) == 28u, "border_px offset");
_Static_assert(offsetof(HoloSurfaceCmd, bg_rgba) == 32u, "bg_rgba offset");
_Static_assert(offsetof(HoloSurfaceCmd, border_rgba) == 36u, "border_rgba offset");
_Static_assert(offsetof(HoloSurfaceCmd, flags) == 40u, "flags offset");

static void test_init_and_magic(void) {
    _Alignas(64) HoloWebGpuRingHeader hdr;
    _Alignas(64) HoloSurfaceCmd slots[8];

    /* Non-power-of-two must fail */
    CHECK(holo_webgpu_ring_init(&hdr, slots, 0u) == HOLO_WEBGPU_ERR_INVALID);
    CHECK(holo_webgpu_ring_init(&hdr, slots, 6u) == HOLO_WEBGPU_ERR_INVALID);

    CHECK(holo_webgpu_ring_init(&hdr, slots, 8u) == HOLO_WEBGPU_OK);
    CHECK(hdr.magic == B8G_ABI_MAGIC_V3);
    CHECK(hdr.magic == 0x33564742u);
    CHECK(hdr.version == 1u);
    CHECK(hdr.capacity == 8u);
    CHECK(hdr.head == 0u);
    CHECK(hdr.tail == 0u);
    CHECK(hdr.frame_seq == 0u);
    CHECK(holo_webgpu_ring_count(&hdr) == 0u);
    CHECK(holo_webgpu_arena_bytes(8u) == 64u + 8u * 64u);
    CHECK(((uintptr_t)&hdr % 64u) == 0u);
    CHECK(((uintptr_t)&slots[0] % 64u) == 0u);

    /* Corrupt magic must be rejected */
    HoloSurfaceCmd out;
    hdr.magic = 0xDEADBEEFu;
    CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out) == HOLO_WEBGPU_ERR_MAGIC);
}

static void test_surface_theme_commit_roundtrip(void) {
    _Alignas(64) HoloWebGpuRingHeader hdr;
    _Alignas(64) HoloSurfaceCmd slots[16];
    CHECK(holo_webgpu_ring_init(&hdr, slots, 16u) == HOLO_WEBGPU_OK);

    /* 1. Submit theme palette */
    uint32_t bg = holo_rgba(0x10u, 0x14u, 0x22u, 0xFFu);
    uint32_t accent = holo_rgba(0x38u, 0xBDu, 0xF8u, 0xFFu);
    uint32_t accent2 = holo_rgba(0xA8u, 0x55u, 0xF7u, 0xFFu);
    CHECK(holo_webgpu_submit_theme(&hdr, slots, bg, accent, accent2, HOLO_RING_FLAG_NONE) == HOLO_WEBGPU_OK);
    CHECK(hdr.theme_bg_rgba == bg);
    CHECK(hdr.theme_accent_rgba == accent);
    CHECK(hdr.theme_accent2_rgba == accent2);

    /* 2. Submit surface quad */
    HoloSurfaceCmd surf;
    memset(&surf, 0, sizeof(surf));
    surf.surface_id   = 42u;
    surf.x            = 120.5f;
    surf.y            = 80.25f;
    surf.w            = 640.0f;
    surf.h            = 420.0f;
    surf.radius       = 14.0f;
    surf.border_px    = 2.0f;
    surf.bg_rgba      = holo_rgba(0x1Eu, 0x29u, 0x3Bu, 0xD9u);
    surf.border_rgba  = accent;
    surf.flags        = HOLO_SURFACE_FLAG_ACTIVE | HOLO_SURFACE_FLAG_BLUR;
    surf.z_index      = 3.0f;
    surf.opacity      = 0.95f;
    CHECK(holo_webgpu_submit_surface(&hdr, slots, &surf) == HOLO_WEBGPU_OK);

    /* 3. Submit cursor */
    HoloSurfaceCmd cursor;
    memset(&cursor, 0, sizeof(cursor));
    cursor.opcode     = HOLO_CMD_CURSOR;
    cursor.surface_id = 1u;
    cursor.x          = 300.0f;
    cursor.y          = 200.0f;
    cursor.w          = 16.0f;
    cursor.h          = 16.0f;
    cursor.opacity    = 1.0f;
    CHECK(holo_webgpu_submit_cmd(&hdr, slots, &cursor) == HOLO_WEBGPU_OK);

    /* 4. Commit frame */
    uint32_t seq = 0u;
    CHECK(holo_webgpu_commit_frame(&hdr, slots, &seq) == HOLO_WEBGPU_OK);
    CHECK(seq == 1u);
    CHECK(hdr.frame_seq == 1u);
    CHECK(holo_webgpu_ring_count(&hdr) == 4u);

    /* Pop and verify FIFO order */
    HoloSurfaceCmd out;
    CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out) == HOLO_WEBGPU_OK);
    CHECK(out.opcode == HOLO_CMD_THEME_PALETTE);
    CHECK(out.bg_rgba == bg);
    CHECK(out.border_rgba == accent);
    CHECK(out.accent2_rgba == accent2);

    CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out) == HOLO_WEBGPU_OK);
    CHECK(out.opcode == HOLO_CMD_SURFACE_QUAD);
    CHECK(out.surface_id == 42u);
    CHECK(out.x == 120.5f && out.y == 80.25f && out.w == 640.0f && out.h == 420.0f);
    CHECK(out.radius == 14.0f && out.border_px == 2.0f);
    CHECK(out.flags == (HOLO_SURFACE_FLAG_ACTIVE | HOLO_SURFACE_FLAG_BLUR));
    CHECK(out.z_index == 3.0f);

    CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out) == HOLO_WEBGPU_OK);
    CHECK(out.opcode == HOLO_CMD_CURSOR);
    CHECK(out.x == 300.0f && out.y == 200.0f);

    CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out) == HOLO_WEBGPU_OK);
    CHECK(out.opcode == HOLO_CMD_COMMIT_FRAME);
    CHECK(out.surface_id == 1u);

    CHECK(holo_webgpu_ring_count(&hdr) == 0u);
    CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out) == HOLO_WEBGPU_ERR_EMPTY);
}

static void test_ring_wraparound_and_backpressure(void) {
    _Alignas(64) HoloWebGpuRingHeader hdr;
    _Alignas(64) HoloSurfaceCmd slots[4];
    CHECK(holo_webgpu_ring_init(&hdr, slots, 4u) == HOLO_WEBGPU_OK);

    /* Fill capacity=4, 5th must return FULL and increment dropped_cmds */
    for (uint32_t i = 0u; i < 4u; i++) {
        HoloSurfaceCmd c;
        memset(&c, 0, sizeof(c));
        c.surface_id = 100u + i;
        c.w = 100.0f;
        c.h = 50.0f;
        CHECK(holo_webgpu_submit_surface(&hdr, slots, &c) == HOLO_WEBGPU_OK);
    }
    CHECK(holo_webgpu_ring_count(&hdr) == 4u);

    HoloSurfaceCmd extra;
    memset(&extra, 0, sizeof(extra));
    extra.surface_id = 999u;
    CHECK(holo_webgpu_submit_surface(&hdr, slots, &extra) == HOLO_WEBGPU_ERR_FULL);
    CHECK(hdr.dropped_cmds == 1u);

    /* Pop 2, push 2 more across multiple wraparound cycles */
    for (uint32_t round = 0u; round < 10u; round++) {
        HoloSurfaceCmd out0, out1;
        CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out0) == HOLO_WEBGPU_OK);
        CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out1) == HOLO_WEBGPU_OK);
        CHECK(out0.surface_id == 100u + round * 2u);
        CHECK(out1.surface_id == 100u + round * 2u + 1u);

        for (uint32_t k = 0u; k < 2u; k++) {
            HoloSurfaceCmd c;
            memset(&c, 0, sizeof(c));
            c.surface_id = 104u + round * 2u + k;
            CHECK(holo_webgpu_submit_surface(&hdr, slots, &c) == HOLO_WEBGPU_OK);
        }
        CHECK(holo_webgpu_ring_count(&hdr) == 4u);
    }

    /* Drain remaining 4 */
    for (uint32_t i = 0u; i < 4u; i++) {
        HoloSurfaceCmd out;
        CHECK(holo_webgpu_pop_cmd(&hdr, slots, &out) == HOLO_WEBGPU_OK);
        CHECK(out.surface_id == 120u + i);
    }
    CHECK(holo_webgpu_ring_count(&hdr) == 0u);
    CHECK(hdr.head == 24u && hdr.tail == 24u);
}

int main(void) {
    test_init_and_magic();
    test_surface_theme_commit_roundtrip();
    test_ring_wraparound_and_backpressure();

    if (g_checks_failed != 0) {
        fprintf(stderr, "holo_display_webgpu_test: %d/%d checks FAILED\n",
                g_checks_failed, g_checks_run);
        return 1;
    }

    printf("holo_display_webgpu_test: PASS (%d/%d checks, B8G_ABI_MAGIC_V3=0x%08X, 64B aligned)\n",
           g_checks_run, g_checks_run, B8G_ABI_MAGIC_V3);
    return 0;
}
