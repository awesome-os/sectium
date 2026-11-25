#ifndef SECTIUM_DRIVER_H
#define SECTIUM_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SECTIUM_ABI_MAGIC_V3 0x33564742u

typedef enum {
  SECTIUM_RENDERER_SKIA        = 1,
  SECTIUM_RENDERER_DAWN_WEBGPU = 2,
  SECTIUM_RENDERER_BLINK       = 3,
  SECTIUM_RENDERER_QT          = 4,
  SECTIUM_RENDERER_TTY_KITTY   = 5
} sectium_renderer_kind_t;

typedef enum {
  SECTIUM_CHANNEL_COMMAND = 1,
  SECTIUM_CHANNEL_DISPLAY = 2
} sectium_channel_t;

typedef struct {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} sectium_rect_t;

typedef struct {
  uint32_t abi_magic;
  uint32_t surface_id;
  sectium_renderer_kind_t renderer;
  sectium_rect_t bounds;
  uint32_t stride_bytes;
  uint32_t flags;
} sectium_surface_t;

typedef struct {
  sectium_rect_t left;
  sectium_rect_t divider;
  sectium_rect_t right;
} sectium_split_layout_t;

uint32_t sectium_driver_abi_magic(void);
const char* sectium_driver_default_pipe(void);
const char* sectium_renderer_name(sectium_renderer_kind_t kind);
int sectium_validate_pipe_addr(const char* addr);
int sectium_surface_init(
  sectium_surface_t* out,
  uint32_t surface_id,
  sectium_renderer_kind_t renderer,
  int32_t width,
  int32_t height
);
int sectium_layout_split(
  int32_t sidebar_w,
  int32_t topbar_h,
  int32_t total_w,
  int32_t total_h,
  float ratio,
  int32_t divider_w,
  sectium_split_layout_t* out
);

#ifdef __cplusplus
}
#endif

#endif /* SECTIUM_DRIVER_H */
