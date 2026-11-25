# Sectium (`awesome-os/sectium`)

> **The Universal Display Driver, Post-Chromium Compositor & Multi-Renderer Window Manager for Awesome-OS (`B8G_ABI_MAGIC_V3 = 0x33564742`, `v11` CAS).**

Sectium is **not** just a browser — it is the single universal display driver that unifies **Skia**, **Dawn WebGPU**, **Blink**, **Qt**, and **Kitty-Ink TTY** behind one lock-free `B8G_ARENA_V1` surface ABI and one native tiling/split window manager (`\\.\pipe\sectium-dev` / `/tmp/sectium-dev.sock`, zero `127.0.0.1` HTTP IPC).

---

## Quick Install & Run (Windows / Linux / macOS)

```sh
# Install / hydrate from v11 CAS via holo-pm (hardlinks to ./bin/)
holo-pm install repository://github.com/awesome-os/sectium@main

# Or project & run directly via holo-project (0-byte hardlink)
holo-project "project://cpp/sectium?from=repository://github.com/awesome-os/sectium@main"
```

---

## Unified Renderers & Host Backends

| Layer | Unified Backends | Source (`repository://`) |
| :--- | :--- | :--- |
| **2D / Vector / Text** | `SECTIUM_RENDERER_SKIA` | `repository://github.com/awesome-os/skia@main` |
| **3D / GPU Compute** | `SECTIUM_RENDERER_DAWN_WEBGPU` | `repository://github.com/awesome-os/dawn@main` |
| **Web / DOM Engine** | `SECTIUM_RENDERER_BLINK` | `repository://github.com/awesome-os/blink@main` |
| **Native UI Toolkit** | `SECTIUM_RENDERER_QT` | `repository://github.com/awesome-os/qt@main` |
| **Zero-GPU Terminal** | `SECTIUM_RENDERER_TTY_KITTY` | `repository://github.com/awesome-os/awesome-os@main` |
| **Electron Browser Shell** | `electron-sectium-browser-shell` (`holo-display-electron`) | `repository://github.com/awesome-os/sectium-browser@main` |
| **Desktop & Greeter Host** | `awesome-os-desktop` (`holo-display-win32` / `wayland`) | `repository://github.com/awesome-os/awesome-os-desktop@main` |

---

## Verification

```powershell
# Run C11 Universal Display Driver test (native Win64 via zig cc)
zig cc -std=c11 -O2 -Wall -Wextra -Werror -I include src/sectium_driver.c tests/sectium_driver_test.c -o sectium_driver_test.exe && .\sectium_driver_test.exe

# Run TypeScript manifest & renderer registry tests (Node 22+, zero deps)
node --test sectium.test.mts
```
