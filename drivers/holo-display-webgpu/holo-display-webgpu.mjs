/**
 * Paravirtual `holo-display-webgpu` Composition Driver (Host / Browser ESM)
 *
 * Zero external dependencies. Shares the exact 64-byte aligned `B8G_ARENA_V1`
 * `SharedArrayBuffer` (or `ArrayBuffer` fallback) ring layout with `holo_display_webgpu.h`:
 *   - Bytes [0..63]:                 HoloWebGpuRingHeader (16 x uint32)
 *   - Bytes [64 .. 64 + cap*64 - 1]: HoloSurfaceCmd[cap]  (16 x uint32/float32 each)
 *
 * Backends (in priority order):
 *   1. WebGPU (`navigator.gpu` + WGSL SDF rounded-rect + Hyprland border glow + aurora)
 *   2. WebGL2 (GLSL ES 3.00 SDF rounded-rect + aurora fallback)
 *   3. Canvas2D (2D glass + glow + aurora fallback)
 */

export const B8G_ABI_MAGIC_V3 = 0x33564742;
export const HOLO_WEBGPU_RING_VERSION = 1;
export const HOLO_RING_HEADER_BYTES = 64;
export const HOLO_CMD_BYTES = 64;
export const HOLO_CMD_STRIDE_U32 = 16;

export const HOLO_CMD_NONE = 0;
export const HOLO_CMD_SURFACE_QUAD = 1;
export const HOLO_CMD_THEME_PALETTE = 2;
export const HOLO_CMD_COMMIT_FRAME = 3;
export const HOLO_CMD_CURSOR = 4;

export const HOLO_RING_FLAG_NONE = 0;
export const HOLO_RING_FLAG_LIGHT_THEME = 1 << 0;
export const HOLO_RING_FLAG_DIRTY = 1 << 1;
export const HOLO_RING_FLAG_WALLPAPER = 1 << 2;

export const HOLO_SURFACE_FLAG_NONE = 0;
export const HOLO_SURFACE_FLAG_ACTIVE = 1 << 0;
export const HOLO_SURFACE_FLAG_FLOATING = 1 << 1;
export const HOLO_SURFACE_FLAG_BLUR = 1 << 2;

/* Ring header u32 word indices (matches HoloWebGpuRingHeader) */
const HDR_MAGIC = 0;
const HDR_VERSION = 1;
const HDR_CAPACITY = 2;
const HDR_HEAD = 3;
const HDR_TAIL = 4;
const HDR_FRAME_SEQ = 5;
const HDR_THEME_ACCENT = 6;
const HDR_THEME_BG = 7;
const HDR_FLAGS = 8;
const HDR_THEME_ACCENT2 = 9;
const HDR_DROPPED = 10;

/* Command slot u32/f32 word offsets (matches HoloSurfaceCmd) */
const CMD_OPCODE = 0;
const CMD_SURFACE_ID = 1;
const CMD_X = 2;
const CMD_Y = 3;
const CMD_W = 4;
const CMD_H = 5;
const CMD_RADIUS = 6;
const CMD_BORDER_PX = 7;
const CMD_BG_RGBA = 8;
const CMD_BORDER_RGBA = 9;
const CMD_FLAGS = 10;
const CMD_ACCENT2_RGBA = 11;
const CMD_Z_INDEX = 12;
const CMD_OPACITY = 13;

const MAX_GPU_SURFACES = 256;

export function packRgba(r, g, b, a = 255) {
  return (((r & 0xff) << 24) | ((g & 0xff) << 16) | ((b & 0xff) << 8) | (a & 0xff)) >>> 0;
}

export function unpackRgbaNorm(rgba) {
  const u = rgba >>> 0;
  return [
    ((u >>> 24) & 0xff) / 255,
    ((u >>> 16) & 0xff) / 255,
    ((u >>> 8) & 0xff) / 255,
    (u & 0xff) / 255,
  ];
}

export function rgbaToCss(rgba, alphaScale = 1) {
  const u = rgba >>> 0;
  const r = (u >>> 24) & 0xff;
  const g = (u >>> 16) & 0xff;
  const b = (u >>> 8) & 0xff;
  const a = ((u & 0xff) / 255) * alphaScale;
  return `rgba(${r}, ${g}, ${b}, ${Math.max(0, Math.min(1, a)).toFixed(4)})`;
}

export function parseHexRgba(hex, defaultRgba = 0x0b0f19ff) {
  if (typeof hex === 'number' && Number.isFinite(hex)) {
    return hex >>> 0;
  }
  if (typeof hex !== 'string') {
    return defaultRgba >>> 0;
  }
  let s = hex.trim();
  if (s.startsWith('#')) s = s.slice(1);
  if (s.length === 3) {
    const r = parseInt(s[0] + s[0], 16);
    const g = parseInt(s[1] + s[1], 16);
    const b = parseInt(s[2] + s[2], 16);
    return packRgba(r, g, b, 255);
  }
  if (s.length === 6) {
    const v = parseInt(s, 16);
    return Number.isNaN(v) ? defaultRgba >>> 0 : ((v << 8) | 0xff) >>> 0;
  }
  if (s.length === 8) {
    const v = parseInt(s, 16);
    return Number.isNaN(v) ? defaultRgba >>> 0 : v >>> 0;
  }
  return defaultRgba >>> 0;
}

function createRingStorage(capacity, externalBuffer) {
  const isPow2 = capacity > 0 && (capacity & (capacity - 1)) === 0;
  const cap = isPow2 ? capacity : 256;
  const totalBytes = HOLO_RING_HEADER_BYTES + cap * HOLO_CMD_BYTES;
  let buffer = externalBuffer || null;
  let shared = false;

  if (!buffer) {
    if (typeof SharedArrayBuffer === 'function') {
      try {
        buffer = new SharedArrayBuffer(totalBytes);
        shared = true;
      } catch {
        buffer = new ArrayBuffer(totalBytes);
      }
    } else {
      buffer = new ArrayBuffer(totalBytes);
    }
  } else {
    shared = typeof SharedArrayBuffer === 'function' && buffer instanceof SharedArrayBuffer;
  }

  const headerU32 = new Uint32Array(buffer, 0, HOLO_RING_HEADER_BYTES / 4);
  const slotsU32 = new Uint32Array(buffer, HOLO_RING_HEADER_BYTES, cap * HOLO_CMD_STRIDE_U32);
  const slotsF32 = new Float32Array(buffer, HOLO_RING_HEADER_BYTES, cap * HOLO_CMD_STRIDE_U32);

  const loadU32 = (idx) => (shared ? Atomics.load(headerU32, idx) : headerU32[idx]);
  const storeU32 = (idx, val) => {
    const u = val >>> 0;
    if (shared) Atomics.store(headerU32, idx, u);
    else headerU32[idx] = u;
  };

  if (loadU32(HDR_MAGIC) !== B8G_ABI_MAGIC_V3) {
    headerU32.fill(0);
    slotsU32.fill(0);
    storeU32(HDR_VERSION, HOLO_WEBGPU_RING_VERSION);
    storeU32(HDR_CAPACITY, cap);
    storeU32(HDR_HEAD, 0);
    storeU32(HDR_TAIL, 0);
    storeU32(HDR_FRAME_SEQ, 0);
    storeU32(HDR_THEME_ACCENT, packRgba(0x7c, 0x3a, 0xed, 0xff));
    storeU32(HDR_THEME_BG, packRgba(0x0b, 0x0f, 0x19, 0xff));
    storeU32(HDR_THEME_ACCENT2, packRgba(0x06, 0xb6, 0xd4, 0xff));
    storeU32(HDR_FLAGS, HOLO_RING_FLAG_NONE);
    storeU32(HDR_MAGIC, B8G_ABI_MAGIC_V3);
  }

  return { buffer, shared, capacity: cap, headerU32, slotsU32, slotsF32, loadU32, storeU32 };
}

const WGSL_SHADER = /* wgsl */ `
struct FrameUniforms {
  resolution : vec2<f32>,
  time       : f32,
  surfaceCnt : u32,
  bgColor    : vec4<f32>,
  accent1    : vec4<f32>,
  accent2    : vec4<f32>,
  flags      : u32,
  hasWp      : u32,
  pad0       : u32,
  pad1       : u32,
};

struct SurfaceGpu {
  rect       : vec4<f32>, /* x, y, w, h */
  params     : vec4<f32>, /* radius, border_px, z_index, opacity */
  bgColor    : vec4<f32>,
  borderCol  : vec4<f32>,
  accent2Col : vec4<f32>,
  meta       : vec4<u32>, /* surface_id, flags, 0, 0 */
};

@group(0) @binding(0) var<uniform> uFrame : FrameUniforms;
@group(0) @binding(1) var<storage, read> uSurfaces : array<SurfaceGpu>;
@group(0) @binding(2) var uWpSampler : sampler;
@group(0) @binding(3) var uWpTex : texture_2d<f32>;

struct VsOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) uv : vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vid : u32) -> VsOut {
  var positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>( 3.0, -1.0),
    vec2<f32>(-1.0,  3.0)
  );
  var out : VsOut;
  let p = positions[vid];
  out.pos = vec4<f32>(p, 0.0, 1.0);
  out.uv = vec2<f32>(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  return out;
}

fn sdRoundedBox(p : vec2<f32>, halfSize : vec2<f32>, r : f32) -> f32 {
  let cr = min(r, min(halfSize.x, halfSize.y));
  let q = abs(p) - halfSize + vec2<f32>(cr, cr);
  return length(max(q, vec2<f32>(0.0))) + min(max(q.x, q.y), 0.0) - cr;
}

fn renderAurora(uv : vec2<f32>) -> vec3<f32> {
  let t = uFrame.time * 0.35;
  let isLight = (uFrame.flags & 1u) != 0u;
  let c1 = vec2<f32>(0.25 + 0.18 * sin(t * 0.9), 0.30 + 0.16 * cos(t * 0.7));
  let c2 = vec2<f32>(0.76 + 0.16 * cos(t * 0.8), 0.68 + 0.18 * sin(t * 0.6));
  let g1 = exp(-dot(uv - c1, uv - c1) * 4.5);
  let g2 = exp(-dot(uv - c2, uv - c2) * 4.2);
  let wave = 0.5 + 0.5 * sin((uv.x * 3.1 + uv.y * 2.3) + t * 1.4);
  let strength = select(0.32, 0.18, isLight);
  var col = uFrame.bgColor.rgb;
  col = mix(col, uFrame.accent1.rgb, clamp(g1 * strength, 0.0, 0.65));
  col = mix(col, uFrame.accent2.rgb, clamp(g2 * strength * (0.75 + 0.25 * wave), 0.0, 0.65));
  return col;
}

@fragment
fn fs_main(in : VsOut) -> @location(0) vec4<f32> {
  let px = in.uv * uFrame.resolution;
  var col = renderAurora(in.uv);

  if (uFrame.hasWp != 0u) {
    let wp = textureSample(uWpTex, uWpSampler, in.uv);
    col = mix(col, wp.rgb, wp.a * 0.88);
  }

  let count = min(uFrame.surfaceCnt, ${MAX_GPU_SURFACES}u);
  for (var i : u32 = 0u; i < count; i = i + 1u) {
    let s = uSurfaces[i];
    let halfSize = s.rect.zw * 0.5;
    if (halfSize.x <= 0.5 || halfSize.y <= 0.5) {
      continue;
    }
    let center = s.rect.xy + halfSize;
    let p = px - center;
    let d = sdRoundedBox(p, halfSize, s.params.x);
    let flags = s.meta.y;
    let isActive = (flags & 1u) != 0u;
    let isFloating = (flags & 2u) != 0u;
    let hasBlur = (flags & 4u) != 0u;
    let opacity = clamp(s.params.w, 0.0, 1.0);

    /* Outer shadow / Hyprland active border glow */
    if (d > 0.0 && d < 36.0) {
      let shadowSpread = select(18.0, 28.0, isFloating || isActive);
      let shadowAlpha = exp(-d / shadowSpread) * select(0.28, 0.42, isFloating) * opacity;
      col = mix(col, vec3<f32>(0.0, 0.0, 0.0), shadowAlpha * 0.65);

      if (isActive && d < 22.0) {
        let ang = atan2(p.y, p.x) + uFrame.time * 1.4;
        let gradT = 0.5 + 0.5 * sin(ang);
        let glowCol = mix(s.borderCol.rgb, uFrame.accent2.rgb, gradT);
        let glow = exp(-d * 0.22) * 0.52 * opacity;
        col = col + glowCol * glow;
      }
    }

    /* Surface interior + border */
    let aa = 1.0 - smoothstep(-0.75, 0.75, d);
    if (aa > 0.001) {
      var fillRgb = s.bgColor.rgb;
      var fillA = s.bgColor.a * opacity;
      if (hasBlur) {
        /* Frosted glass highlight + subtle top sheen */
        let sheen = clamp(1.0 - ((px.y - s.rect.y) / max(s.rect.w, 1.0)), 0.0, 1.0);
        let frosted = mix(col, fillRgb, clamp(fillA, 0.25, 0.92));
        fillRgb = frosted + vec3<f32>(0.06, 0.07, 0.09) * sheen * 0.5;
        fillA = 1.0;
      }

      var surfRgb = mix(col, fillRgb, fillA);

      let bpx = max(s.params.y, 0.0);
      if (bpx > 0.01 && d > -(bpx + 0.75)) {
        let borderMask = smoothstep(-(bpx + 0.75), -(bpx - 0.75), d);
        var bCol = s.borderCol.rgb;
        if (isActive) {
          let ang = atan2(p.y, p.x) + uFrame.time * 1.8;
          let gradT = 0.5 + 0.5 * sin(ang);
          bCol = mix(s.borderCol.rgb, uFrame.accent2.rgb, gradT);
        }
        surfRgb = mix(surfRgb, bCol, borderMask * s.borderCol.a * opacity);
      }

      col = mix(col, surfRgb, aa);
    }
  }

  return vec4<f32>(col, 1.0);
}
`;

const GLSL_VS = `#version 300 es
precision highp float;
out vec2 vUv;
void main() {
  vec2 pos = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);
  gl_Position = vec4(pos, 0.0, 1.0);
  vUv = vec2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
}
`;

const GLSL_FS = `#version 300 es
precision highp float;
in vec2 vUv;
out vec4 fragColor;
uniform vec2 uResolution;
uniform float uTime;
uniform vec4 uBg;
uniform vec4 uAccent1;
uniform vec4 uAccent2;
uniform int uIsLight;
uniform int uCount;
uniform vec4 uRects[32];
uniform vec4 uParams[32];
uniform vec4 uBgCols[32];
uniform vec4 uBorderCols[32];
uniform int uFlags[32];

float sdRoundedBox(vec2 p, vec2 b, float r) {
  float cr = min(r, min(b.x, b.y));
  vec2 q = abs(p) - b + vec2(cr);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - cr;
}

void main() {
  vec2 px = vUv * uResolution;
  float t = uTime * 0.35;
  vec2 c1 = vec2(0.25 + 0.18 * sin(t * 0.9), 0.30 + 0.16 * cos(t * 0.7));
  vec2 c2 = vec2(0.76 + 0.16 * cos(t * 0.8), 0.68 + 0.18 * sin(t * 0.6));
  float g1 = exp(-dot(vUv - c1, vUv - c1) * 4.5);
  float g2 = exp(-dot(vUv - c2, vUv - c2) * 4.2);
  float strength = uIsLight != 0 ? 0.18 : 0.32;
  vec3 col = uBg.rgb;
  col = mix(col, uAccent1.rgb, clamp(g1 * strength, 0.0, 0.65));
  col = mix(col, uAccent2.rgb, clamp(g2 * strength, 0.0, 0.65));

  for (int i = 0; i < 32; i++) {
    if (i >= uCount) break;
    vec4 r = uRects[i];
    vec2 hs = r.zw * 0.5;
    if (hs.x <= 0.5 || hs.y <= 0.5) continue;
    vec2 p = px - (r.xy + hs);
    float d = sdRoundedBox(p, hs, uParams[i].x);
    int fl = uFlags[i];
    bool active = (fl & 1) != 0;
    float op = clamp(uParams[i].w, 0.0, 1.0);
    if (d > 0.0 && d < 22.0 && active) {
      float glow = exp(-d * 0.22) * 0.5 * op;
      col += mix(uBorderCols[i].rgb, uAccent2.rgb, 0.5) * glow;
    }
    float aa = 1.0 - smoothstep(-0.75, 0.75, d);
    if (aa > 0.001) {
      vec3 surf = mix(col, uBgCols[i].rgb, uBgCols[i].a * op);
      float bpx = uParams[i].y;
      if (bpx > 0.01 && d > -(bpx + 0.75)) {
        float bm = smoothstep(-(bpx + 0.75), -(bpx - 0.75), d);
        surf = mix(surf, uBorderCols[i].rgb, bm * uBorderCols[i].a * op);
      }
      col = mix(col, surf, aa);
    }
  }
  fragColor = vec4(col, 1.0);
}
`;

/**
 * Create a Paravirtual `holo-display-webgpu` compositor bound to `canvas`.
 *
 * @param {HTMLCanvasElement|OffscreenCanvas} canvas
 * @param {{ capacity?: number, buffer?: SharedArrayBuffer|ArrayBuffer }} [options]
 */
export function createHoloWebGpuCompositor(canvas, options = {}) {
  const ring = createRingStorage(options.capacity || 256, options.buffer || null);
  const surfaces = new Map();
  const startMs = typeof performance !== 'undefined' ? performance.now() : Date.now();

  const state = {
    backend: 'uninitialized',
    initialized: false,
    bgRgba: ring.loadU32(HDR_THEME_BG),
    accentRgba: ring.loadU32(HDR_THEME_ACCENT),
    accent2Rgba: ring.loadU32(HDR_THEME_ACCENT2),
    isLight: (ring.loadU32(HDR_FLAGS) & HOLO_RING_FLAG_LIGHT_THEME) !== 0,
    wallpaper: null,
    wallpaperDirty: false,
    framesRendered: 0,
    cmdsProcessed: 0,
    lastFrameMs: 0,
    cursor: { x: 0, y: 0, visible: false },
  };

  /* Backend-specific handles */
  let gpu = null;
  let gl2 = null;
  let ctx2d = null;

  function enqueueCmd(opcode, fields) {
    const head = ring.loadU32(HDR_HEAD);
    const tail = ring.loadU32(HDR_TAIL);
    if (((head - tail) >>> 0) >= ring.capacity) {
      ring.storeU32(HDR_DROPPED, ring.loadU32(HDR_DROPPED) + 1);
      return false;
    }
    const base = (head & (ring.capacity - 1)) * HOLO_CMD_STRIDE_U32;
    ring.slotsU32.fill(0, base, base + HOLO_CMD_STRIDE_U32);
    ring.slotsU32[base + CMD_OPCODE] = opcode >>> 0;
    ring.slotsU32[base + CMD_SURFACE_ID] = (fields.surface_id ?? fields.surfaceId ?? 0) >>> 0;
    ring.slotsF32[base + CMD_X] = Number(fields.x ?? 0);
    ring.slotsF32[base + CMD_Y] = Number(fields.y ?? 0);
    ring.slotsF32[base + CMD_W] = Number(fields.w ?? 0);
    ring.slotsF32[base + CMD_H] = Number(fields.h ?? 0);
    ring.slotsF32[base + CMD_RADIUS] = Number(fields.radius ?? 12);
    ring.slotsF32[base + CMD_BORDER_PX] = Number(fields.border_px ?? fields.borderPx ?? 1.5);
    ring.slotsU32[base + CMD_BG_RGBA] = parseHexRgba(fields.bg_rgba ?? fields.bgRgba, packRgba(0x18, 0x20, 0x32, 0xd8));
    ring.slotsU32[base + CMD_BORDER_RGBA] = parseHexRgba(fields.border_rgba ?? fields.borderRgba, state.accentRgba);
    ring.slotsU32[base + CMD_FLAGS] = (fields.flags ?? 0) >>> 0;
    ring.slotsU32[base + CMD_ACCENT2_RGBA] = parseHexRgba(fields.accent2_rgba ?? fields.accent2Rgba, state.accent2Rgba);
    ring.slotsF32[base + CMD_Z_INDEX] = Number(fields.z_index ?? fields.zIndex ?? 0);
    ring.slotsF32[base + CMD_OPACITY] = fields.opacity === undefined ? 1.0 : Number(fields.opacity);

    ring.storeU32(HDR_HEAD, (head + 1) >>> 0);
    return true;
  }

  function drainRing() {
    let tail = ring.loadU32(HDR_TAIL);
    const head = ring.loadU32(HDR_HEAD);
    const mask = ring.capacity - 1;
    let processed = 0;

    while (tail !== head) {
      const base = (tail & mask) * HOLO_CMD_STRIDE_U32;
      const opcode = ring.slotsU32[base + CMD_OPCODE];

      if (opcode === HOLO_CMD_SURFACE_QUAD) {
        const id = ring.slotsU32[base + CMD_SURFACE_ID];
        const w = ring.slotsF32[base + CMD_W];
        const h = ring.slotsF32[base + CMD_H];
        if (w <= 0 || h <= 0) {
          surfaces.delete(id);
        } else {
          surfaces.set(id, {
            surfaceId: id,
            x: ring.slotsF32[base + CMD_X],
            y: ring.slotsF32[base + CMD_Y],
            w,
            h,
            radius: ring.slotsF32[base + CMD_RADIUS],
            borderPx: ring.slotsF32[base + CMD_BORDER_PX],
            bgRgba: ring.slotsU32[base + CMD_BG_RGBA],
            borderRgba: ring.slotsU32[base + CMD_BORDER_RGBA],
            flags: ring.slotsU32[base + CMD_FLAGS],
            accent2Rgba: ring.slotsU32[base + CMD_ACCENT2_RGBA],
            zIndex: ring.slotsF32[base + CMD_Z_INDEX],
            opacity: ring.slotsF32[base + CMD_OPACITY] || 1.0,
          });
        }
      } else if (opcode === HOLO_CMD_THEME_PALETTE) {
        state.bgRgba = ring.slotsU32[base + CMD_BG_RGBA];
        state.accentRgba = ring.slotsU32[base + CMD_BORDER_RGBA];
        state.accent2Rgba = ring.slotsU32[base + CMD_ACCENT2_RGBA];
        state.isLight = (ring.slotsU32[base + CMD_FLAGS] & HOLO_RING_FLAG_LIGHT_THEME) !== 0;
      } else if (opcode === HOLO_CMD_CURSOR) {
        state.cursor.x = ring.slotsF32[base + CMD_X];
        state.cursor.y = ring.slotsF32[base + CMD_Y];
        state.cursor.visible = ring.slotsF32[base + CMD_W] > 0;
      }

      tail = (tail + 1) >>> 0;
      processed++;
    }

    if (processed > 0) {
      ring.storeU32(HDR_TAIL, tail);
      state.cmdsProcessed += processed;
    }
    return processed;
  }

  function getSortedSurfaces() {
    const list = Array.from(surfaces.values());
    list.sort((a, b) => (a.zIndex - b.zIndex) || (a.surfaceId - b.surfaceId));
    return list;
  }

  async function initWebGpu() {
    if (typeof navigator === 'undefined' || !navigator.gpu || !canvas) return false;
    try {
      const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
      if (!adapter) return false;
      const device = await adapter.requestDevice();
      const context = canvas.getContext('webgpu');
      if (!context) return false;

      const format = navigator.gpu.getPreferredCanvasFormat();
      context.configure({ device, format, alphaMode: 'premultiplied' });

      const shaderModule = device.createShaderModule({ code: WGSL_SHADER });
      const pipeline = device.createRenderPipeline({
        layout: 'auto',
        vertex: { module: shaderModule, entryPoint: 'vs_main' },
        fragment: { module: shaderModule, entryPoint: 'fs_main', targets: [{ format }] },
        primitive: { topology: 'triangle-list' },
      });

      const uniformBuffer = device.createBuffer({
        size: 96,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      });

      /* 96 bytes per SurfaceGpu (6 x vec4) */
      const surfaceBuffer = device.createBuffer({
        size: MAX_GPU_SURFACES * 96,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
      });

      const sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });
      const fallbackTex = device.createTexture({
        size: [1, 1, 1],
        format: 'rgba8unorm',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
      });
      device.queue.writeTexture(
        { texture: fallbackTex },
        new Uint8Array([0, 0, 0, 0]),
        { bytesPerRow: 4 },
        [1, 1, 1],
      );

      const bindGroup = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: uniformBuffer } },
          { binding: 1, resource: { buffer: surfaceBuffer } },
          { binding: 2, resource: sampler },
          { binding: 3, resource: fallbackTex.createView() },
        ],
      });

      gpu = {
        device,
        context,
        format,
        pipeline,
        uniformBuffer,
        surfaceBuffer,
        sampler,
        fallbackTex,
        wpTex: null,
        bindGroup,
        uniformScratch: new ArrayBuffer(96),
        surfaceScratch: new ArrayBuffer(MAX_GPU_SURFACES * 96),
      };
      state.backend = 'webgpu';
      return true;
    } catch {
      gpu = null;
      return false;
    }
  }

  function initWebGl2() {
    if (!canvas) return false;
    try {
      const gl = canvas.getContext('webgl2', { antialias: true, alpha: true });
      if (!gl) return false;
      const compile = (type, src) => {
        const sh = gl.createShader(type);
        gl.shaderSource(sh, src);
        gl.compileShader(sh);
        if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
          gl.deleteShader(sh);
          return null;
        }
        return sh;
      };
      const vs = compile(gl.VERTEX_SHADER, GLSL_VS);
      const fs = compile(gl.FRAGMENT_SHADER, GLSL_FS);
      if (!vs || !fs) return false;
      const prog = gl.createProgram();
      gl.attachShader(prog, vs);
      gl.attachShader(prog, fs);
      gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) return false;

      gl2 = {
        gl,
        prog,
        loc: {
          resolution: gl.getUniformLocation(prog, 'uResolution'),
          time: gl.getUniformLocation(prog, 'uTime'),
          bg: gl.getUniformLocation(prog, 'uBg'),
          accent1: gl.getUniformLocation(prog, 'uAccent1'),
          accent2: gl.getUniformLocation(prog, 'uAccent2'),
          isLight: gl.getUniformLocation(prog, 'uIsLight'),
          count: gl.getUniformLocation(prog, 'uCount'),
          rects: gl.getUniformLocation(prog, 'uRects'),
          params: gl.getUniformLocation(prog, 'uParams'),
          bgCols: gl.getUniformLocation(prog, 'uBgCols'),
          borderCols: gl.getUniformLocation(prog, 'uBorderCols'),
          flags: gl.getUniformLocation(prog, 'uFlags'),
        },
      };
      state.backend = 'webgl2';
      return true;
    } catch {
      gl2 = null;
      return false;
    }
  }

  function initCanvas2d() {
    if (!canvas) return false;
    const c = canvas.getContext('2d');
    if (!c) return false;
    ctx2d = c;
    state.backend = 'canvas2d';
    return true;
  }

  function syncWallpaperGpu() {
    if (!gpu || !state.wallpaperDirty) return;
    state.wallpaperDirty = false;
    if (gpu.wpTex) {
      gpu.wpTex.destroy();
      gpu.wpTex = null;
    }
    let activeView = gpu.fallbackTex.createView();
    if (state.wallpaper && state.wallpaper.width > 0 && state.wallpaper.height > 0) {
      try {
        const tex = gpu.device.createTexture({
          size: [state.wallpaper.width, state.wallpaper.height, 1],
          format: 'rgba8unorm',
          usage:
            GPUTextureUsage.TEXTURE_BINDING |
            GPUTextureUsage.COPY_DST |
            GPUTextureUsage.RENDER_ATTACHMENT,
        });
        gpu.device.queue.copyExternalImageToTexture(
          { source: state.wallpaper },
          { texture: tex },
          [state.wallpaper.width, state.wallpaper.height],
        );
        gpu.wpTex = tex;
        activeView = tex.createView();
      } catch {
        gpu.wpTex = null;
      }
    }
    gpu.bindGroup = gpu.device.createBindGroup({
      layout: gpu.pipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: gpu.uniformBuffer } },
        { binding: 1, resource: { buffer: gpu.surfaceBuffer } },
        { binding: 2, resource: gpu.sampler },
        { binding: 3, resource: activeView },
      ],
    });
  }

  function renderWebGpu(sorted, elapsedSec) {
    syncWallpaperGpu();
    const w = canvas.width || 1280;
    const h = canvas.height || 720;
    const count = Math.min(sorted.length, MAX_GPU_SURFACES);

    const uf = new Float32Array(gpu.uniformScratch);
    const uu = new Uint32Array(gpu.uniformScratch);
    uf[0] = w;
    uf[1] = h;
    uf[2] = elapsedSec;
    uu[3] = count;
    const bg = unpackRgbaNorm(state.bgRgba);
    const a1 = unpackRgbaNorm(state.accentRgba);
    const a2 = unpackRgbaNorm(state.accent2Rgba);
    uf.set(bg, 4);
    uf.set(a1, 8);
    uf.set(a2, 12);
    uu[16] = state.isLight ? HOLO_RING_FLAG_LIGHT_THEME : 0;
    uu[17] = gpu.wpTex ? 1 : 0;
    uu[18] = 0;
    uu[19] = 0;
    gpu.device.queue.writeBuffer(gpu.uniformBuffer, 0, gpu.uniformScratch);

    if (count > 0) {
      const sf = new Float32Array(gpu.surfaceScratch);
      const su = new Uint32Array(gpu.surfaceScratch);
      for (let i = 0; i < count; i++) {
        const s = sorted[i];
        const o = i * 24;
        sf[o + 0] = s.x;
        sf[o + 1] = s.y;
        sf[o + 2] = s.w;
        sf[o + 3] = s.h;
        sf[o + 4] = s.radius;
        sf[o + 5] = s.borderPx;
        sf[o + 6] = s.zIndex;
        sf[o + 7] = s.opacity;
        sf.set(unpackRgbaNorm(s.bgRgba), o + 8);
        sf.set(unpackRgbaNorm(s.borderRgba), o + 12);
        sf.set(unpackRgbaNorm(s.accent2Rgba), o + 16);
        su[o + 20] = s.surfaceId >>> 0;
        su[o + 21] = s.flags >>> 0;
        su[o + 22] = 0;
        su[o + 23] = 0;
      }
      gpu.device.queue.writeBuffer(gpu.surfaceBuffer, 0, gpu.surfaceScratch, 0, count * 96);
    }

    const encoder = gpu.device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [
        {
          view: gpu.context.getCurrentTexture().createView(),
          clearValue: { r: bg[0], g: bg[1], b: bg[2], a: 1 },
          loadOp: 'clear',
          storeOp: 'store',
        },
      ],
    });
    pass.setPipeline(gpu.pipeline);
    pass.setBindGroup(0, gpu.bindGroup);
    pass.draw(3, 1, 0, 0);
    pass.end();
    gpu.device.queue.submit([encoder.finish()]);
  }

  function renderWebGl2(sorted, elapsedSec) {
    const { gl, prog, loc } = gl2;
    const w = canvas.width || 1280;
    const h = canvas.height || 720;
    gl.viewport(0, 0, w, h);
    gl.useProgram(prog);
    gl.uniform2f(loc.resolution, w, h);
    gl.uniform1f(loc.time, elapsedSec);
    gl.uniform4fv(loc.bg, unpackRgbaNorm(state.bgRgba));
    gl.uniform4fv(loc.accent1, unpackRgbaNorm(state.accentRgba));
    gl.uniform4fv(loc.accent2, unpackRgbaNorm(state.accent2Rgba));
    gl.uniform1i(loc.isLight, state.isLight ? 1 : 0);

    const count = Math.min(sorted.length, 32);
    gl.uniform1i(loc.count, count);
    if (count > 0) {
      const rects = new Float32Array(count * 4);
      const params = new Float32Array(count * 4);
      const bgCols = new Float32Array(count * 4);
      const borderCols = new Float32Array(count * 4);
      const flags = new Int32Array(count);
      for (let i = 0; i < count; i++) {
        const s = sorted[i];
        rects.set([s.x, s.y, s.w, s.h], i * 4);
        params.set([s.radius, s.borderPx, s.zIndex, s.opacity], i * 4);
        bgCols.set(unpackRgbaNorm(s.bgRgba), i * 4);
        borderCols.set(unpackRgbaNorm(s.borderRgba), i * 4);
        flags[i] = s.flags | 0;
      }
      gl.uniform4fv(loc.rects, rects);
      gl.uniform4fv(loc.params, params);
      gl.uniform4fv(loc.bgCols, bgCols);
      gl.uniform4fv(loc.borderCols, borderCols);
      gl.uniform1iv(loc.flags, flags);
    }
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  function renderCanvas2d(sorted, elapsedSec) {
    const ctx = ctx2d;
    const w = canvas.width || 1280;
    const h = canvas.height || 720;

    ctx.save();
    ctx.fillStyle = rgbaToCss(state.bgRgba);
    ctx.fillRect(0, 0, w, h);

    /* Animated aurora backdrop */
    const t = elapsedSec * 0.35;
    const g1x = w * (0.25 + 0.18 * Math.sin(t * 0.9));
    const g1y = h * (0.3 + 0.16 * Math.cos(t * 0.7));
    const grad1 = ctx.createRadialGradient(g1x, g1y, 10, g1x, g1y, Math.max(w, h) * 0.55);
    grad1.addColorStop(0, rgbaToCss(state.accentRgba, state.isLight ? 0.22 : 0.34));
    grad1.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = grad1;
    ctx.fillRect(0, 0, w, h);

    const g2x = w * (0.76 + 0.16 * Math.cos(t * 0.8));
    const g2y = h * (0.68 + 0.18 * Math.sin(t * 0.6));
    const grad2 = ctx.createRadialGradient(g2x, g2y, 10, g2x, g2y, Math.max(w, h) * 0.55);
    grad2.addColorStop(0, rgbaToCss(state.accent2Rgba, state.isLight ? 0.2 : 0.3));
    grad2.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = grad2;
    ctx.fillRect(0, 0, w, h);

    if (state.wallpaper) {
      try {
        ctx.globalAlpha = 0.88;
        ctx.drawImage(state.wallpaper, 0, 0, w, h);
        ctx.globalAlpha = 1;
      } catch {
        /* ignore unloaded image */
      }
    }

    for (const s of sorted) {
      const isActive = (s.flags & HOLO_SURFACE_FLAG_ACTIVE) !== 0;
      const isFloating = (s.flags & HOLO_SURFACE_FLAG_FLOATING) !== 0;
      ctx.save();
      ctx.globalAlpha = Math.max(0, Math.min(1, s.opacity));
      if (isActive || isFloating) {
        ctx.shadowColor = isActive ? rgbaToCss(s.borderRgba, 0.55) : 'rgba(0,0,0,0.45)';
        ctx.shadowBlur = isActive ? 22 : 18;
      }
      ctx.beginPath();
      if (typeof ctx.roundRect === 'function') {
        ctx.roundRect(s.x, s.y, s.w, s.h, s.radius);
      } else {
        ctx.rect(s.x, s.y, s.w, s.h);
      }
      ctx.fillStyle = rgbaToCss(s.bgRgba);
      ctx.fill();
      if (s.borderPx > 0) {
        ctx.lineWidth = s.borderPx;
        ctx.strokeStyle = rgbaToCss(s.borderRgba);
        ctx.stroke();
      }
      ctx.restore();
    }
    ctx.restore();
  }

  return {
    ringBuffer: ring.buffer,
    headerU32: ring.headerU32,
    slotsU32: ring.slotsU32,
    slotsF32: ring.slotsF32,

    async init() {
      if (state.initialized) return state.backend;
      if (!(await initWebGpu())) {
        if (!initWebGl2()) {
          if (!initCanvas2d()) {
            state.backend = 'headless';
          }
        }
      }
      state.initialized = true;
      return state.backend;
    },

    submitSurface(cmd) {
      return enqueueCmd(HOLO_CMD_SURFACE_QUAD, cmd || {});
    },

    setTheme(bgHex, accentHex, accent2Hex, isLight = false) {
      const bg = parseHexRgba(bgHex, state.bgRgba);
      const accent = parseHexRgba(accentHex, state.accentRgba);
      const accent2 = parseHexRgba(accent2Hex, state.accent2Rgba);
      const flags = (isLight ? HOLO_RING_FLAG_LIGHT_THEME : HOLO_RING_FLAG_NONE) | HOLO_RING_FLAG_DIRTY;

      ring.storeU32(HDR_THEME_BG, bg);
      ring.storeU32(HDR_THEME_ACCENT, accent);
      ring.storeU32(HDR_THEME_ACCENT2, accent2);
      ring.storeU32(HDR_FLAGS, flags);

      return enqueueCmd(HOLO_CMD_THEME_PALETTE, {
        bg_rgba: bg,
        border_rgba: accent,
        accent2_rgba: accent2,
        flags,
      });
    },

    setWallpaper(imgOrNull) {
      state.wallpaper = imgOrNull || null;
      state.wallpaperDirty = true;
      const curFlags = ring.loadU32(HDR_FLAGS);
      const nextFlags = state.wallpaper
        ? curFlags | HOLO_RING_FLAG_WALLPAPER
        : curFlags & ~HOLO_RING_FLAG_WALLPAPER;
      ring.storeU32(HDR_FLAGS, nextFlags);
    },

    commitFrame() {
      const nextSeq = (ring.loadU32(HDR_FRAME_SEQ) + 1) >>> 0;
      enqueueCmd(HOLO_CMD_COMMIT_FRAME, { surface_id: nextSeq });
      ring.storeU32(HDR_FRAME_SEQ, nextSeq);

      const t0 = typeof performance !== 'undefined' ? performance.now() : Date.now();
      drainRing();
      const sorted = getSortedSurfaces();
      const elapsedSec = (t0 - startMs) / 1000;

      if (state.backend === 'webgpu' && gpu) {
        renderWebGpu(sorted, elapsedSec);
      } else if (state.backend === 'webgl2' && gl2) {
        renderWebGl2(sorted, elapsedSec);
      } else if (state.backend === 'canvas2d' && ctx2d) {
        renderCanvas2d(sorted, elapsedSec);
      }

      const t1 = typeof performance !== 'undefined' ? performance.now() : Date.now();
      state.lastFrameMs = t1 - t0;
      state.framesRendered++;
      return nextSeq;
    },

    getStats() {
      return {
        magic: ring.loadU32(HDR_MAGIC),
        version: ring.loadU32(HDR_VERSION),
        backend: state.backend,
        sharedMemory: ring.shared,
        capacity: ring.capacity,
        head: ring.loadU32(HDR_HEAD),
        tail: ring.loadU32(HDR_TAIL),
        frameSeq: ring.loadU32(HDR_FRAME_SEQ),
        droppedCmds: ring.loadU32(HDR_DROPPED),
        surfaceCount: surfaces.size,
        framesRendered: state.framesRendered,
        cmdsProcessed: state.cmdsProcessed,
        lastFrameMs: state.lastFrameMs,
      };
    },
  };
}
