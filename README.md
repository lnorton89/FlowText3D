# Flow Text 3D

A Windows 11 screensaver: extruded 3D text, like the classic 3D Text screensaver, floating over a smooth flowing gradient background. It covers every monitor. It's one native `.scr` file (Win32 + OpenGL), and it needs no installer or runtime.

## Install

1. Copy `FlowText3D.scr` to `C:\Windows\System32` (you'll need admin rights). You can also leave it in any folder and right-click it, then choose **Install**.
2. Open **Settings > Personalization > Lock screen > Screen saver** (or run `control desk.cpl,,@screensaver`).
3. Pick **FlowText3D** in the list, then click **Settings...** to customize it. Click **Preview** to test it.

The file is unsigned, so Windows SmartScreen may warn the first time: choose **More info > Run anyway**.
To uninstall, delete the file. Settings are stored in `HKCU\Software\FlowText3D`.

Right-clicking the `.scr` also offers **Test** (full screen) and **Configure** (settings).

## Settings

The settings dialog has a live preview that updates as you change things.

| Group | Option | What it does |
|---|---|---|
| Text | Text / Clock | Your own text (any Unicode characters the font has), or the current time |
| | Font... | Any TrueType/OpenType font installed on Windows (default: Segoe UI Black) |
| 3D text | Rotation | None, Gentle sway, Spin, See-saw, Wobble, Tumble, Flip |
| | Motion | Stay centered, Wander (smooth drifting), Bounce |
| | Surface | **Chrome** (reflects the background colours), **Solid colour** (choose a colour), **Rainbow**, **Pearl** (iridescent) |
| | Size / Depth / Speed | Text size, how deep the extrusion is, animation speed |
| Background | Palette | Bloom, Aurora, Sunset, Ocean, Neon, Lava, Forest, Candy, Monochrome, or **Custom** (4 colours of your own) |
| | Flow speed / Swirl / Brightness | How fast it moves, how much it curls, how bright it is |
| Display | Monitors | **Each monitor its own text**, **Same text on every monitor**, **One text across all monitors**, **Text on main monitor only** |
| | Quality | Background render resolution: Best / Balanced / Battery saver (the gradient is smooth, so Balanced looks the same as Best) |
| | Edges | Anti-aliasing for the text: Off / 4x / 8x |
| | Frame rate | Match the display (vsync), or 30 fps to save power |

**Test full screen** runs the screensaver with the current (unsaved) settings. Move the mouse or press a key to return.

## How multi-monitor works

The screensaver opens one borderless window covering the whole virtual desktop and draws each monitor into its own viewport:

- The background is computed in desktop pixel coordinates, so the flow continues seamlessly across monitors, even when they have different resolutions or are offset from each other.
- Each monitor gets its own camera frustum. In "One text across all monitors" mode each monitor shows its slice of one big off-axis frustum, so the text crosses the bezel correctly.
- The app is per-monitor DPI aware (PerMonitorV2), so 4K + 1080p setups render at native resolution on both.

## Build from source

Build with MinGW-w64, either from Linux/WSL or from MSYS2 on Windows:

```sh
# Ubuntu / WSL
sudo apt install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64
./build.sh                       # -> dist/FlowText3D.scr

# MSYS2 (MINGW64 shell) on Windows
pacman -S mingw-w64-x86_64-gcc
CC=gcc WINDRES=windres ./build.sh
```

| File | Contents |
|---|---|
| `flowtext3d.c` | Everything: settings and registry, GL loader, GLSL shaders, glyph cache, renderer, window and input handling, settings dialog |
| `flowtext3d.rc`, `resource.h` | Settings dialog layout, version info, icon, manifest |
| `flowtext3d.manifest` | Common Controls v6 (modern dialog look) and PerMonitorV2 DPI awareness |
| `make_icon.py` | Regenerates `flowtext3d.ico` (Pillow) |

### Rendering notes
- The text uses `wglUseFontOutlinesW`, which turns real font outlines into extruded polygons. Characters are built on demand, so any character in the font works.
- Lighting normals come from screen-space derivatives in the fragment shader, so shading doesn't depend on the driver's glyph normals.
- The background is a two-level domain-warped noise field rendered into a 16-bit float texture, upscaled with bilinear filtering and dithered per monitor, so it never shows gradient banding.
- The text is anti-aliased by rendering each monitor into a multisampled offscreen buffer and resolving it to the screen (this keeps memory use reasonable on large multi-monitor desktops).
- Fallbacks: without framebuffer objects it renders the background directly. With only OpenGL 1.1 (e.g. "Microsoft Basic Display Adapter" in a VM with no GPU driver) it falls back to fixed-function lighting and a simple gradient.

### Testing aid
Set `FLOWTEXT3D_MONITORS=x,y,w,h;x,y,w,h` (window pixels, first one is primary) to override the detected monitor layout. This is useful for checking multi-monitor modes in a VM or under Wine. Wine's `wglUseFontOutlines` draws flat faces only, so letter depth only shows on real Windows.
