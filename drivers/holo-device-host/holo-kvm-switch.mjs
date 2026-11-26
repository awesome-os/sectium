/**
 * holo-kvm-switch.mjs — Virtual KVM Switch + Zero-Copy Shared Pure-Lib Pool
 *
 * - Shares read-only pure libs (compiled WebAssembly.Module, WGSL shaders, font/theme
 *   atlases) ONCE in RAM across all virtual devices (0 duplication).
 * - Strictly isolates KVM Monitor output & HID input per device, supporting instant
 *   atomic switching (Ctrl+Alt+1..8) and split Monitor vs. HID routing.
 */

import { createHoloVirtualDevice } from './holo-device-host.mjs';

export function createHoloKvmSwitch() {
  const devices = new Map();
  const sharedLibs = new Map(); // sha512 -> { name, sha512, bytes, resource, attachedDevices: Set }
  let activeMonitorId = null;
  let activeHidId = null;
  let switchCount = 0;

  return {
    registerDevice(id, opts = {}) {
      const dev = {
        id,
        name: opts.name || id,
        device: createHoloVirtualDevice(opts),
        attachedLibs: new Set(),
      };
      devices.set(id, dev);
      if (activeMonitorId === null) activeMonitorId = id;
      if (activeHidId === null) activeHidId = id;
      return dev;
    },

    /** Register an immutable read-only pure lib / WASM Module / shader ONCE by SHA-512 */
    registerSharedPureLib({ name, sha512, bytes, resource }) {
      if (!sha512) throw new Error('Shared pure lib requires a v11 SHA-512 digest');
      if (!sharedLibs.has(sha512)) {
        sharedLibs.set(sha512, { name, sha512, bytes, resource, attachedDevices: new Set() });
      }
      return sharedLibs.get(sha512);
    },

    attachSharedLib(deviceId, sha512) {
      const dev = devices.get(deviceId);
      const lib = sharedLibs.get(sha512);
      if (!dev || !lib) return false;
      dev.attachedLibs.add(sha512);
      lib.attachedDevices.add(deviceId);
      return true;
    },

    switchBoth(deviceId) {
      if (!devices.has(deviceId)) return false;
      activeMonitorId = deviceId;
      activeHidId = deviceId;
      switchCount++;
      return true;
    },

    switchMonitor(deviceId) {
      if (!devices.has(deviceId)) return false;
      activeMonitorId = deviceId;
      switchCount++;
      return true;
    },

    switchHid(deviceId) {
      if (!devices.has(deviceId)) return false;
      activeHidId = deviceId;
      switchCount++;
      return true;
    },

    /** Strict HID isolation: ONLY activeHidId receives input events */
    routeHid(type, code, modifiers, x = 0, y = 0, button = 0) {
      const dev = devices.get(activeHidId);
      if (!dev) return false;
      return dev.device.pushHid(type, code, modifiers, x, y, button);
    },

    getStats() {
      let sharedBytes = 0;
      let savedBytes = 0;
      for (const lib of sharedLibs.values()) {
        if (lib.attachedDevices.size > 0) sharedBytes += lib.bytes;
        if (lib.attachedDevices.size > 1) savedBytes += (lib.attachedDevices.size - 1) * lib.bytes;
      }
      return {
        abiMagic: '0x33564742',
        deviceCount: devices.size,
        activeMonitorId,
        activeHidId,
        switchCount,
        sharedLibCount: sharedLibs.size,
        sharedBytes,
        savedBytes,
      };
    },
  };
}
