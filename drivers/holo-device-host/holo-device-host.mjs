/**
 * holo-device-host.mjs — Unified Virtual Device Host (Monitor + HID Input + v11 Storage)
 *
 * Exposes identical B8G_ABI_MAGIC_V3 (0x33564742) device channels to apps running
 * inside either a Native Frameless Sectium Device Monitor or a Browser Virtual Device.
 */

export const HOLO_DEVICE_ABI_MAGIC = 0x33564742;
export const HOLO_HID_KEY_DOWN = 1;
export const HOLO_HID_KEY_UP = 2;
export const HOLO_HID_POINTER_MOVE = 3;
export const HOLO_HID_POINTER_DOWN = 4;
export const HOLO_HID_POINTER_UP = 5;
export const HOLO_HID_SUPER_LOCK = 8;

export function createHoloVirtualDevice(opts = {}) {
  const capacity = opts.hidCapacity || 128;
  const arenaBytes = 64 + capacity * 64;
  const shared = typeof SharedArrayBuffer !== 'undefined';
  const buf = shared ? new SharedArrayBuffer(arenaBytes) : new ArrayBuffer(arenaBytes);
  const u32 = new Uint32Array(buf);
  const f32 = new Float32Array(buf);

  // Initialize 64-byte HoloHidRingHeader
  u32[0] = HOLO_DEVICE_ABI_MAGIC;
  u32[1] = 1; // version
  u32[2] = capacity;
  u32[3] = 0; // head
  u32[4] = 0; // tail
  u32[5] = 0; // dropped
  u32[6] = 0; // active_mods
  u32[7] = opts.flags || 0;
  u32[8] = opts.width || (typeof window !== 'undefined' ? window.innerWidth : 1440);
  u32[9] = opts.height || (typeof window !== 'undefined' ? window.innerHeight : 900);

  function pushHid(type, code, modifiers, x = 0, y = 0, button = 0) {
    const head = shared ? Atomics.load(u32, 3) : u32[3];
    const tail = shared ? Atomics.load(u32, 4) : u32[4];
    if (head - tail >= capacity) {
      u32[5] = (u32[5] + 1) >>> 0;
      return false;
    }
    const base = 16 + (head & (capacity - 1)) * 16;
    u32[base + 0] = type;
    u32[base + 1] = code >>> 0;
    u32[base + 2] = modifiers >>> 0;
    u32[base + 3] = button >>> 0;
    f32[base + 4] = x;
    f32[base + 5] = y;
    u32[base + 10] = (head + 1) >>> 0;
    u32[6] = modifiers >>> 0;
    if (shared) Atomics.store(u32, 3, (head + 1) >>> 0);
    else u32[3] = (head + 1) >>> 0;
    return true;
  }

  return {
    abiMagic: '0x33564742',
    sharedMemory: shared,
    hidBuffer: buf,
    pushHid,
    getStats() {
      return {
        abiMagic: '0x33564742',
        sharedMemory: shared,
        head: u32[3],
        tail: u32[4],
        dropped: u32[5],
        activeMods: u32[6],
        monitor: { width: u32[8], height: u32[9] },
      };
    },
  };
}
