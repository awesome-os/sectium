#include "sectium_driver.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__); return 1; } } while (0)

int main(void) {
  CHECK(sectium_driver_abi_magic() == 0x33564742u);
  CHECK(strcmp(sectium_renderer_name(SECTIUM_RENDERER_SKIA), "skia") == 0);
  CHECK(strcmp(sectium_renderer_name(SECTIUM_RENDERER_DAWN_WEBGPU), "dawn-webgpu") == 0);
  CHECK(strcmp(sectium_renderer_name(SECTIUM_RENDERER_BLINK), "blink") == 0);
  CHECK(strcmp(sectium_renderer_name(SECTIUM_RENDERER_QT), "qt") == 0);
  CHECK(strcmp(sectium_renderer_name(SECTIUM_RENDERER_TTY_KITTY), "tty-kitty") == 0);

  /* Pipe B8G016 validation */
  CHECK(sectium_validate_pipe_addr(sectium_driver_default_pipe()) == 0);
  CHECK(sectium_validate_pipe_addr("http://127.0.0.1:8080") < 0);
  CHECK(sectium_validate_pipe_addr("localhost:3000") < 0);

  /* 64-byte aligned B8G_ARENA_V1 surface stride */
  sectium_surface_t surf;
  CHECK(sectium_surface_init(&surf, 1, SECTIUM_RENDERER_SKIA, 1360, 860) == 0);
  CHECK(surf.abi_magic == 0x33564742u);
  CHECK((surf.stride_bytes % 64u) == 0u);

  /* Heterogeneous split layout (e.g. Blink Web Tab | Dawn 3D Viewport) */
  sectium_split_layout_t split;
  CHECK(sectium_layout_split(52, 44, 1360, 860, 0.5f, 6, &split) == 0);
  CHECK(split.left.x == 52);
  CHECK(split.divider.x == split.left.x + split.left.width);
  CHECK(split.right.x == split.divider.x + 6);
  CHECK(split.left.width + split.divider.width + split.right.width == 1360 - 52);

  printf("PASS: sectium universal display driver (abi=0x%08X pipe=%s stride=%u split=%d|%d)\n",
    sectium_driver_abi_magic(), sectium_driver_default_pipe(), surf.stride_bytes,
    split.left.width, split.right.width);
  return 0;
}
