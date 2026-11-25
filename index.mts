import manifestDoc from './sectium.manifest.json' with { type: 'json' };

export const SECTIUM_ABI_MAGIC_V3 = '0x33564742';

export function loadSectiumManifest() {
  return manifestDoc;
}

export function getSectiumRenderer(id: string) {
  return manifestDoc.renderers.find((r) => r.id === id);
}

export function getSectiumShell(id: string) {
  return manifestDoc.shells.find((s) => s.id === id);
}
