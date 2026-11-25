import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import {
  SECTIUM_ABI_MAGIC_V3,
  getSectiumRenderer,
  getSectiumShell,
  loadSectiumManifest,
} from './index.mts';

describe('awesome-os/sectium Universal Display Driver', () => {
  it('unifies Skia, Dawn WebGPU, Blink, Qt, and Kitty-Ink TTY under B8G v3 + v11 CAS', () => {
    const m = loadSectiumManifest();
    assert.equal(m.schema, 'awesome-os-sectium-driver-manifest-v1');
    assert.equal(m.abiMagic, SECTIUM_ABI_MAGIC_V3);
    assert.equal(m.casLayout, 'v11');
    assert.equal(m.renderers.length, 5);
    assert.equal(getSectiumRenderer('skia')?.repository, 'repository://github.com/awesome-os/skia@main');
    assert.equal(getSectiumRenderer('dawn-webgpu')?.repository, 'repository://github.com/awesome-os/dawn@main');
    assert.equal(getSectiumShell('electron-sectium-browser-shell')?.displayBackend, 'holo-display-electron');
    assert.equal(getSectiumShell('awesome-os-desktop')?.displayBackend, 'holo-display-win32');
  });
});
