#include "sectium_driver.h"

#include <string.h>

uint32_t sectium_driver_abi_magic(void) {
  return SECTIUM_ABI_MAGIC_V3;
}

const char* sectium_driver_default_pipe(void) {
#ifdef _WIN32
  return "\\\\.\\pipe\\sectium-dev";
#else
  return "/tmp/sectium-dev.sock";
#endif
}

const char* sectium_renderer_name(sectium_renderer_kind_t kind) {
  switch (kind) {
    case SECTIUM_RENDERER_SKIA:        return "skia";
    case SECTIUM_RENDERER_DAWN_WEBGPU: return "dawn-webgpu";
    case SECTIUM_RENDERER_BLINK:       return "blink";
    case SECTIUM_RENDERER_QT:          return "qt";
    case SECTIUM_RENDERER_TTY_KITTY:   return "tty-kitty";
    default:                           return "unknown";
  }
}

int sectium_validate_pipe_addr(const char* addr) {
  if (!addr || addr[0] == '\0') return -1;
  /* Enforce B8G016: reject local HTTP/TCP loopback IPC */
  if (strstr(addr, "127.0.0.1") != NULL) return -2;
  if (strstr(addr, "localhost") != NULL) return -2;
  if (strncmp(addr, "http://", 7) == 0 || strncmp(addr, "https://", 8) == 0) return -2;
  return 0;
}

int sectium_surface_init(
  sectium_surface_t* out,
  uint32_t surface_id,
  sectium_renderer_kind_t renderer,
  int32_t width,
  int32_t height
) {
  if (!out || width <= 0 || height <= 0) return -1;
  out->abi_magic = SECTIUM_ABI_MAGIC_V3;
  out->surface_id = surface_id;
  out->renderer = renderer;
  out->bounds.x = 0;
  out->bounds.y = 0;
  out->bounds.width = width;
  out->bounds.height = height;
  /* 64-byte cache-line aligned RGBA8 stride for B8G_ARENA_V1 zero-copy */
  uint32_t raw_stride = (uint32_t)width * 4u;
  out->stride_bytes = (raw_stride + 63u) & ~63u;
  out->flags = 0u;
  return 0;
}

int sectium_layout_split(
  int32_t sidebar_w,
  int32_t topbar_h,
  int32_t total_w,
  int32_t total_h,
  float ratio,
  int32_t divider_w,
  sectium_split_layout_t* out
) {
  if (!out || total_w <= sidebar_w + divider_w + 2 || total_h <= topbar_h) return -1;
  if (ratio < 0.2f) ratio = 0.2f;
  if (ratio > 0.8f) ratio = 0.8f;

  int32_t avail_w = total_w - sidebar_w - divider_w;
  int32_t avail_h = total_h - topbar_h;
  int32_t left_w = (int32_t)((float)avail_w * ratio);
  if (left_w < 1) left_w = 1;
  int32_t right_w = avail_w - left_w;
  if (right_w < 1) right_w = 1;

  out->left.x = sidebar_w;
  out->left.y = topbar_h;
  out->left.width = left_w;
  out->left.height = avail_h;

  out->divider.x = sidebar_w + left_w;
  out->divider.y = topbar_h;
  out->divider.width = divider_w;
  out->divider.height = avail_h;

  out->right.x = sidebar_w + left_w + divider_w;
  out->right.y = topbar_h;
  out->right.width = right_w;
  out->right.height = avail_h;
  return 0;
}
