/**
 * @file src/html-ts-runtime.mts
 * @description First-class HTML5 `<b8g-*>` Web Components + direct erasable TypeScript
 * execution layer for `awesome-os/sectium` (zero bundler, zero `tsc`/`webpack`/`vite` emit loop).
 *
 * Strictly `erasableSyntaxOnly: true`, Node 22+ built-ins only.
 */

import { stripTypeScriptTypes } from 'node:module';

export const SECTIUM_ABI_MAGIC_V3 = '0x33564742';

export interface SectiumHtmlSurfaceNode {
  readonly tag: string;
  readonly renderer: string;
  readonly id?: string;
  readonly attrs: Readonly<Record<string, string>>;
}

export interface SectiumCompiledHtmlDoc {
  readonly abiMagic: '0x33564742';
  readonly html: string;
  readonly surfaces: readonly SectiumHtmlSurfaceNode[];
  readonly tsScriptsCompiled: number;
}

function parseAttributes(attrText: string): Record<string, string> {
  const out: Record<string, string> = {};
  const re = /([a-zA-Z0-9_:-]+)\s*=\s*"([^"]*)"/g;
  let m: RegExpExecArray | null;
  while ((m = re.exec(attrText)) !== null) {
    if (m[1]) out[m[1]] = m[2] ?? '';
  }
  return out;
}

/**
 * Compiles an HTML5 document containing `<b8g-*>` Web Components and native
 * `<script type="text/typescript">` / `<script type="module" lang="ts">` blocks
 * directly to executable JS + extracted Sectium surface descriptors (zero bundler).
 */
export function compileSectiumHtmlDocument(rawHtml: string): SectiumCompiledHtmlDoc {
  const surfaces: SectiumHtmlSurfaceNode[] = [];
  const tagRe = /<(b8g-[a-z0-9-]+)([^>]*?)\/?>/gi;
  let tm: RegExpExecArray | null;
  while ((tm = tagRe.exec(rawHtml)) !== null) {
    const tag = (tm[1] || '').toLowerCase();
    const attrs = parseAttributes(tm[2] || '');
    surfaces.push({
      tag,
      renderer: attrs['data-renderer'] || 'blink',
      id: attrs.id,
      attrs,
    });
  }

  let tsScriptsCompiled = 0;
  const scriptRe =
    /<script([^>]*?(?:type\s*=\s*"text\/(?:typescript|stealifylang)"|lang\s*=\s*"(?:ts|stealify)")[^>]*)>([\s\S]*?)<\/script>/gi;

  const html = rawHtml.replace(scriptRe, (_full, attrPart: string, body: string) => {
    const js = stripTypeScriptTypes(body, { mode: 'strip' });
    tsScriptsCompiled++;
    const cleanAttrs = attrPart
      .replace(/type\s*=\s*"text\/(?:typescript|stealifylang)"/gi, 'type="module"')
      .replace(/\s*lang\s*=\s*"(?:ts|stealify)"/gi, '');
    return `<script${cleanAttrs}>${js}</script>`;
  });

  return {
    abiMagic: SECTIUM_ABI_MAGIC_V3,
    html,
    surfaces,
    tsScriptsCompiled,
  };
}
