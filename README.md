<p align="center">
  <img src="src/assets/logo_with_text.png" alt="OnPoint" width="480">
</p>

<p align="center">
  <a href="https://github.com/hrueger/onpoint/actions/workflows/build.yml"><img alt="Build" src="https://github.com/hrueger/onpoint/actions/workflows/build.yml/badge.svg"></a>
  <a href="https://github.com/hrueger/onpoint/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/hrueger/onpoint?label=release"></a>
  <img alt="Platform" src="https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-blue">
</p>

**OnPoint** is a follow-spot and fixture position tracking tool for live entertainment. A camera mounted above the stage or next to a fixture streams a view. Operators click on the video to aim fixtures or send performer positions to a lighting console. Outputs are **PosiStageNet (PSN v2)** for consoles that speak PSN (e.g. grandMA3) and **DMX** via sACN or ArtNet for direct fixture control.

## Highlights

- **Three operating modes** — bird's-eye → PSN, bird's-eye → DMX, or co-mounted camera → DMX
- **NDI, Webcam, and Blackmagic DeckLink** video sources
- **MVR import** — loads fixture positions, 3D scene geometry (GLB), and GDTF profiles directly from your show file
- **GDTF library** — browse, import, or download fixture profiles from [GDTF Share](https://gdtf-share.com/); automatic pan/tilt channel and degree-range extraction
- **3D stage view** — OpenGL scene with orbit camera, multiple render modes, and interactive drawing tools for platforms and polygons
- **Height-aware calibration** — click plane height is adjustable per-frame; PSN Y output tracks it; 3D calibration mode for depth-accurate positioning
- **DMX monitor** — live per-universe channel view (table or grid) with fixture annotation
- **Input adapters** — map sACN/ArtNet or MIDI (with CC-learn) to dimmer, zoom, iris, and focus channels
- **Per-universe sACN/ArtNet routing** — independently configure input and output protocol, universe number, and network mode per MVR fixture universe
- **Multi-operator sessions** — mDNS discovery, per-peer tracker assignment, admin role promotion
- **PSN origin alignment** — offset + rotation with interactive drag and "Snap to MVR origin"
- **Light / Dark / System themes**

## Download

Pre-built packages for macOS (DMG), Windows (EXE), and Linux (tar.gz) are on the [Releases](https://github.com/hrueger/onpoint/releases) page. The Linux archive is self-contained — run `run-onpoint.sh` from the extracted folder.

## Quick start

### Stage3D PSN (bird's-eye → PSN)

1. Launch OnPoint, pick a video source from the stream picker.
2. Open **Setup → Calibration…**, click at least four known stage marks, enter their real-world X/Z coordinates (metres), then click **Compute Homography**.
3. Open **Setup → Settings… → Network** to configure the PSN multicast address and port (defaults: `236.10.10.10:56565`).
4. Press keys **1–9** to select a tracker, then **click and hold** on the video to send its position. Scroll the mouse wheel in the video to adjust the click-plane height (Y output).

### Stage3D DMX (bird's-eye → DMX)

1. Import your show file via **File → Import MVR…** to load fixture positions and GDTF profiles automatically.
2. Calibrate as above (Compute Homography).
3. Assign GDTF profiles to fixtures if not already embedded in the MVR, or download them from the **Fixtures → GDTF Library** dialog.
4. Configure per-universe sACN/ArtNet routing in **Setup → Settings… → DMX**.
5. Click on the video — OnPoint solves pan/tilt from each fixture's 3D position and sends DMX.

### Camera2D DMX (co-mounted camera → DMX)

1. Mount a camera on or next to the fixture.
2. Open **Setup → Calibration… → Camera2D**, click known target positions on screen, enter the corresponding pan/tilt values, then click **Compute**.
3. Configure DMX output in **Setup → Settings… → DMX**.
4. Click on the video to aim the fixture directly.

## Calibration

Three schemes are available in the Calibration panel:

| Scheme            | When to use                                                               |
| ----------------- | ------------------------------------------------------------------------- |
| **Rectangle**     | Four floor corners — fastest, single-plane homography                     |
| **Manual points** | Arbitrary floor marks with known X/Z; origin point required               |
| **Rect3D**        | Four floor + four elevated corners; enables depth-accurate 3D calibration |

The **click-plane height** (slider or mouse wheel in the video) defines the Y plane that pixel clicks map to. The 3D view and PSN Y output both follow it in real time.

## 3D stage view

The Stage3D panel renders an OpenGL scene beside the video:

- **Camera controls** — orbit (drag), zoom (scroll), pitch/yaw; one-click presets: Top, Front, FrontTop, Left, Right
- **Render modes** — Flat (solid colors), Shaded (Phong lighting), Wireframe
- **Drawing tools** — draw rectangle or freeform polygon platforms directly in the 3D view; set height, name, and visibility per object
- **MVR scene** — imported trusses, scene objects, and GLB geometry are rendered in the scene; enable/disable per MVR import layer
- **Fixture labels** — optional overlay of fixture names and layer names

Stage objects defined here are also overlaid on the video with perspective-correct outlines.

## MVR & GDTF

### MVR import

OnPoint imports MVR files (**File → Import MVR…**) and extracts:

- Fixture positions, rotations, and DMX addresses
- Scene objects and trusses (with GLB geometry for 3D rendering)
- Layer hierarchy (each layer can be toggled independently)
- Embedded GDTF profiles

Multiple MVR imports per project are supported, each with an independent position offset.

### GDTF library

The **Fixtures → GDTF Library** dialog manages fixture profiles:

- Browse by manufacturer and fixture name
- Import GDTF files from disk
- Download directly from [GDTF Share](https://gdtf-share.com/) (requires a free account)
- Copy GDTFs embedded in an MVR to the local library
- Preview DMX mode footprint, channel list, and 3D geometry

OnPoint automatically extracts pan/tilt channels (coarse + optional 16-bit fine), degree ranges, and mode footprint from each profile.

### Fixtures panel

A live table of all MVR fixtures with:

- Pan/tilt readout in degrees and DMX values
- GDTF assignment per fixture
- Tracker link assignment (which tracker drives which fixture)
- Inline DMX address editing (patch mode)
- Physical / DMX display toggle

## DMX output

### Universe routing

Each fixture universe from your MVR gets its own row in **Settings → DMX**:

| Per-universe setting | Options |
| - | |
| Follow-spots enabled | On / Off (OnPoint computes pan/tilt for this universe) |
| Input protocol | sACN (E1.31), ArtNet |
| Input network mode | Multicast, Unicast, Broadcast |
| Output protocol | sACN (E1.31), ArtNet |
| Output network mode | Multicast, Unicast, Broadcast |
| Output universe number | Same as fixture universe or remapped |
| sACN priority | 0–200 |

### Output modes

| Mode | Behavior |
| -- | |
| **Pan/tilt only** | Only pan and tilt channels are sent; other channels pass through from the console |
| **Replacement** | Full 512-byte universe is received from the console, OnPoint overrides pan/tilt, and the merged frame is forwarded |

### DMX monitor

The DMX Monitor panel shows any configured universe in real time:

- **Table view** — channel number, value (0–255), fixture name, function, and in/out direction
- **Grid view** — visual heatmap of all 512 channels

## Input adapters

Input adapters let you control non-tracked fixture parameters from sACN/ArtNet or MIDI, independently of the console DMX stream. Add adapters in **Settings → Adapters**:

| Parameter | Notes |
| | -- |
| Dimmer | 0–100 % |
| Zoom | 0–100 % |
| Iris | 0–100 % |
| Focus | 0–100 % |
| Click-plane height | Lets a MIDI fader or DMX channel set the tracking height |

**MIDI CC learn** — click the learn button on a mapping, move a fader on your controller, and the CC number is captured automatically.

## PSN output & origin alignment

Packets are sent at **60 Hz** (data) and **1 Hz** (info/name). PSN Y equals the current click-plane height.

| Setting | Default | Description |
| | -- | -- |
| Mode | Multicast | Multicast, Unicast, or Broadcast |
| Multicast IP | `236.10.10.10` | Standard PSN multicast address |
| Port | `56565` | Standard PSN port |

Ensure IGMP snooping is configured on the switch, or enable multicast flooding.

### Origin alignment

When OnPoint's stage coordinate system doesn't match the console's:

- **Offset X/Y/Z** — translate the PSN output origin (metres)
- **Rotation** — rotate CCW around the Y axis (degrees)
- **Snap to MVR origin** — align automatically to the MVR file's world origin
- The **⊕ marker** in the 3D view can be dragged to set the offset interactively

## Multi-operator sessions

Multiple operators connect over the local network via mDNS discovery:

- One machine **hosts** the session; others **join** by picking the session name from the discovery list
- The host assigns tracker IDs (1–9) to each peer
- All operators see every other operator's active tracker overlaid on the video
- **Admin** peers can reassign trackers and promote others; **User** peers are limited to their assigned trackers
- Project state is synchronized across all connected peers

## Keyboard & mouse

| Action | Description |
| | -- |
| Keys **1–9** | Select active tracker |
| **Left click + hold** | Aim selected tracker at stage position |
| **Scroll wheel** (in video) | Adjust click-plane height (5 cm steps) |
| **Escape** | Deselect all trackers |

## Project files

Projects are saved as `.onpoint` JSON files (**File → Save Project**). A project stores:

- Operating mode and video source
- Calibration data (homography, 3D matrix, Camera2D calibration)
- Trackers (ID, name, color)
- Stage objects (geometry, heights, visibility)
- MVR imports (including embedded MVR data and per-layer settings)
- GDTF assignments and fixture tracker links
- DMX universe routing and input adapter configuration
- PSN network settings and origin offset/rotation
- Session tracker assignments
- 3D camera state and render settings

## Development

### Requirements

#### macOS

| Dependency | Version | Install |
| | - | -- |
| macOS | 13+ | — |
| Xcode Command Line Tools | any | `xcode-select --install` |
| CMake | ≥ 3.25 | `brew install cmake` |
| Qt 6 | ≥ 6.4 | `brew install qt` |
| OpenCV | ≥ 4 | `brew install opencv` |
| libarchive | ≥ 3.5 | `brew install libarchive` |
| NDI SDK for Apple | 6.x | [ndi.video](https://ndi.video/for-developers/ndi-sdk/) → macOS |
| Blackmagic DeckLink SDK | 16.x | [blackmagicdesign.com](https://www.blackmagicdesign.com/developer/product/capture-and-playback) — headers bundled in `third_party/` |

#### Windows

| Dependency                | Version | Install                                                                                                                            |
| ------------------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| Windows                   | 10 / 11 | —                                                                                                                                  |
| Visual Studio Build Tools | 2022    | `winget install Microsoft.VisualStudio.2022.BuildTools`                                                                            |
| CMake                     | ≥ 3.25  | `winget install Kitware.CMake`                                                                                                     |
| Qt 6                      | 6.8.3   | `pip install aqtinstall` then `python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtmultimedia --outputdir C:\Qt` |
| OpenCV                    | ≥ 4     | via vcpkg: `vcpkg install opencv:x64-windows`                                                                                      |
| libarchive                | ≥ 3.5   | via vcpkg: `vcpkg install libarchive:x64-windows`                                                                                  |
| NDI SDK for Windows       | 6.x     | [ndi.video](https://ndi.video/for-developers/ndi-sdk/) → Windows                                                                   |
| Blackmagic DeckLink SDK   | 16.x    | headers in `third_party/decklink/Win/include/`                                                                                     |
| Apple Bonjour SDK         | any     | [developer.apple.com](https://developer.apple.com/download/all/?q=Bonjour+SDK+for+Windows)                                         |

### Build

**macOS**

```bash
brew install cmake qt opencv libarchive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
open build/onpoint.app
```

> Tip: pass `-DCMAKE_PREFIX_PATH=/opt/homebrew` if Qt or OpenCV are not found.

**Windows** (PowerShell)

```powershell
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64;C:\vcpkg\installed\x64-windows" `
  -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release --parallel
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\Release\onpoint.exe
```

### Windows installer

```powershell
makensis /DVERSION=x.y.z packaging\windows.nsi
```

## Disclaimer

This project was built with [Claude Code](https://claude.ai/code). It is provided as-is, with no warranties or guarantees of any kind — use it at your own risk, especially in live production environments.

Contributions are very welcome! Feel free to open issues or pull requests.
