import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { compileSectiumHtmlDocument } from '../src/html-ts-runtime.mts';

describe('awesome-os/sectium first-class HTML5 <b8g-*> + direct TypeScript runtime', () => {
  it('extracts <b8g-*> surfaces and strips <script type="text/typescript"> directly with zero bundler', () => {
    const sample = `
<!DOCTYPE html>
<html>
<body>
  <b8g-split id="main" data-ratio="0.5">
    <b8g-surface id="left" data-renderer="blink"></b8g-surface>
    <b8g-surface id="right" data-renderer="dawn-webgpu"></b8g-surface>
  </b8g-split>
  <script type="text/typescript">
    const add = (a: number, b: number): number => a + b;
    const magic: string = "0x33564742";
    void add(19, 23);
    void magic;
  </script>
</body>
</html>`;

    const res = compileSectiumHtmlDocument(sample);
    assert.equal(res.abiMagic, '0x33564742');
    assert.equal(res.tsScriptsCompiled, 1);
    assert.equal(res.surfaces.length, 3);
    assert.equal(res.surfaces[1]?.renderer, 'blink');
    assert.equal(res.surfaces[2]?.renderer, 'dawn-webgpu');
    assert.ok(res.html.includes('type="module"'));
    assert.ok(!res.html.includes(': number'));
    assert.ok(!res.html.includes(': string'));
  });
});
