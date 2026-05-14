# mouse-posi

Follow-spot position tracking for live entertainment. An NDI camera above FOH streams
a view of the stage; operators click and hold on the video to output tracker positions
via **PosiStageNet (PSN v2)** UDP to a grandMA3 console. Multiple operators run the app
simultaneously — each owns one or more tracker IDs (keys **1–9**) and sees all other
operators' positions overlaid on the video.

## Requirements

### macOS

| Dependency               | Version       | Install                                                                                                                                                  |
| ------------------------ | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| macOS                    | 13+ (Ventura) | —                                                                                                                                                        |
| Xcode Command Line Tools | any recent    | `xcode-select --install`                                                                                                                                 |
| CMake                    | ≥ 3.25        | `brew install cmake`                                                                                                                                     |
| Qt 6 (base)              | ≥ 6.4         | `brew install qtbase`                                                                                                                                    |
| Qt SVG                   | ≥ 6.4         | `brew install qtsvg`                                                                                                                                     |
| Qt Multimedia (webcam)   | ≥ 6.4         | `brew install qtmultimedia`                                                                                                                              |
| OpenCV                   | ≥ 4           | `brew install opencv`                                                                                                                                    |
| NDI SDK for Apple        | 6.x           | [ndi.video](https://ndi.video/for-developers/ndi-sdk/) → _Download_ → macOS                                                                              |
| Blackmagic DeckLink SDK  | 16.x          | [blackmagicdesign.com](https://www.blackmagicdesign.com/developer/product/capture-and-playback) → _Download_ → macOS (headers bundled in `third_party/`) |

#### NDI SDK (macOS)

1. Download "NDI SDK for Apple" from ndi.video and run the installer.
2. The installer places the SDK at `/Library/NDI SDK for Apple/`.
3. CMake finds it automatically — no extra steps needed.

---

### Windows

| Dependency                    | Version | Install                                                                                                                                               |
| ----------------------------- | ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Windows                       | 10 / 11 | —                                                                                                                                                     |
| Visual Studio Build Tools     | 2022    | `winget install Microsoft.VisualStudio.2022.BuildTools` — select **Desktop development with C++** workload                                            |
| CMake                         | ≥ 3.25  | `winget install Kitware.CMake`                                                                                                                        |
| Qt 6                          | 6.8.3   | `pip install aqtinstall` then see below                                                                                                               |
| OpenCV                        | ≥ 4     | via vcpkg — see below                                                                                                                                 |
| NDI SDK for Windows           | 6.x     | [ndi.video](https://ndi.video/for-developers/ndi-sdk/) → _Download_ → Windows                                                                         |
| Blackmagic Desktop Video SDK  | 16.x    | [blackmagicdesign.com](https://www.blackmagicdesign.com/developer/product/capture-and-playback) → headers in `third_party/decklink/Win/include/`      |
| Apple Bonjour SDK for Windows | any     | [developer.apple.com](https://developer.apple.com/download/all/?q=Bonjour+SDK+for+Windows) — install to default path (`C:\Program Files\Bonjour SDK`) |

#### Qt 6 (Windows)

Install via `aqtinstall` (no Qt account required):

```bat
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtmultimedia --outputdir C:\Qt
```

Qt ends up at `C:\Qt\6.8.3\msvc2022_64`. Pass this to CMake via `-DCMAKE_PREFIX_PATH`.

#### OpenCV (Windows)

```powershell
winget install Microsoft.Vcpkg
vcpkg integrate install
# On ARM64 Windows you must set the host triplet to avoid a build failure:
vcpkg install opencv:x64-windows --host-triplet=x64-windows
```

Pass `-DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake"` to CMake (see configure command below). You also need to add `C:\vcpkg\installed\x64-windows` to `CMAKE_PREFIX_PATH` so CMake can locate the OpenCV config files.

#### NDI SDK (Windows)

1. Download "NDI 6 SDK for Windows" from ndi.video and run the installer.
2. The installer places the SDK at `C:\Program Files (x86)\NDI\NDI 6 SDK` — CMake finds it automatically.
   Alternatively set `NDI_SDK_DIR` environment variable to the SDK root.

#### Blackmagic DeckLink SDK (Windows)

1. Download the DeckLink SDK from Blackmagic Design (link above).
2. From the archive, copy everything in `Win/include/` to `third_party/decklink/Win/include/`.
3. Generate the COM headers using `midl.exe` (included in the Windows SDK):
    ```bat
    set MIDL="C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\midl.exe"
    set INC=third_party\decklink\Win\include
    %MIDL% /nologo /W1 /char signed /env x64 /h DeckLinkAPI.h /iid DeckLinkAPI_i.c /tlb DeckLinkAPI.tlb /out %INC% /I %INC% /I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um" /I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared" %INC%\DeckLinkAPI.idl
    ```
    This generates `DeckLinkAPI.h` and `DeckLinkAPI_i.c` in `third_party/decklink/Win/include/`.
    _(These generated files are already committed — only redo this if you update the SDK.)_

---

## Build

### macOS

```bash
# Install dependencies (Homebrew)
brew install cmake qtbase qtsvg qtmultimedia opencv

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run
open build/mouse-posi.app
```

> **Tip:** If Qt or OpenCV are not found, pass
> `-DCMAKE_PREFIX_PATH=/opt/homebrew` (Apple Silicon) or `/usr/local` (Intel).

> If you added missing components after a failed configure, clear the cache first:
> `rm -f build/CMakeCache.txt`

### Windows

Open **PowerShell** (or a Developer Command Prompt for VS 2022):

```powershell
cmake -S . -B build `
  -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64;C:\vcpkg\installed\x64-windows" `
  -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

cmake --build build --config Release --parallel
```

The built executable is at `build\Release\mouse-posi.exe`.

After building, deploy the Qt runtime DLLs next to the exe (only needed once):

```powershell
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\Release\mouse-posi.exe
```

> **ARM64 Windows (e.g. Apple Silicon Mac running Windows in a VM):**
> The default MSVC toolset may be too old to cross-compile for x64. Add
> `-T version=14.41,host=ARM64` to the cmake configure command:
> ```powershell
> cmake -S . -B build -A x64 "-T version=14.41,host=ARM64" ...
> ```
> Also ensure you installed the **MSVC v143 build tools** component with x64
> target support in the VS 2022 BuildTools installer.

> **Network share (VM shared folder):** If the source tree lives on a mapped
> network drive, git may refuse to clone the qlementine FetchContent dependency.
> Run once to allow it:
> ```powershell
> git config --global --add safe.directory "%(prefix)///Mac/Home/source/ai_experiments/mouse-posi/build/_deps/qlementine-src"
> ```
> Adjust the path to match your actual mount point.

> Clear the CMake cache after installing missing dependencies:
> `Remove-Item -Recurse -Force build`

---

## First launch

1. The app opens an **NDI source picker**. Select the camera stream and click **Connect**.
2. The main window shows the live video. A yellow banner appears until calibration is done.
3. Open **Setup → Calibration…** to map pixel coordinates to stage coordinates.
4. Open **Setup → Settings…** to configure trackers and PSN network output.

---

## Calibration

Calibration maps pixel positions in the camera view to real-world stage coordinates
(metres). You need at least **four reference marks** at known stage positions (e.g.
floor spikes or lighting fixtures).

1. Open **Setup → Calibration…**
2. Click **+ Click to Add Point**, then click on a known mark in the video.
3. Enter the real-world **Stage X** (stage-right, metres) and **Stage Z** (upstage depth,
   metres) for that point.
4. Repeat for at least four points spread across the stage.
5. Click **Compute Homography** — the reprojection error (in pixels) is shown.
6. Enable **Test mode** and hover over the video to verify the coordinates look correct.
7. Click **OK** to apply. Calibration is saved with the project file.

**Coordinate system:** origin = wherever you choose (e.g. downstage centre).
+X = stage right (from audience), +Z = upstage (away from audience), Y = 0 (floor).

---

## Using trackers

| Action                | Description                                     |
| --------------------- | ----------------------------------------------- |
| Keys **1–9**          | Select the active tracker                       |
| **Left click + hold** | Aim the selected tracker at that stage position |
| **Escape**            | Deselect all trackers                           |

- All operators on the same PSN multicast group see each other's tracker dots overlaid
  on the video.
- Own tracker positions are shown as solid-colour dots; remote operators' dots appear
  lighter.
- The active tracker shows a crosshair: **solid** when the mouse button is held (sending),
  **dashed** when hovering (not sending).

---

## Tracker configuration

Open **Setup → Settings…** or use the **Trackers** panel on the right. Click **+** to add
a tracker, **−** to remove, or **Edit…** (or double-click) to change its ID (1–9), name,
and colour.

---

## Network / PSN settings

Open **Setup → Settings…** → **Network** tab.

| Setting      | Default        | Description                    |
| ------------ | -------------- | ------------------------------ |
| Mode         | Multicast      | Multicast or Unicast           |
| Multicast IP | `236.10.10.10` | Standard PSN multicast address |
| Port         | `56565`        | Standard PSN port              |

PSN packets are sent at **60 Hz** (data packets) and **1 Hz** (info/name packets).
Make sure your network switch has multicast enabled (IGMP snooping configured, or
multicast flooding enabled) and that all machines are on the same subnet.

---

## PSN test listener (Node.js)

If you want to quickly verify PSN packets are on the wire (without grandMA / Wireshark),
there’s a tiny Node.js listener script that prints the decoded system name + tracker data.

```bash
npm install
npm run psn:listen
```

Optional environment variables:

```bash
# defaults shown
PSN_GROUP=236.10.10.10 PSN_PORT=56565 PSN_BIND=0.0.0.0 npm run psn:listen

# if your machine has multiple network interfaces, you may need to pin the multicast iface
PSN_IFACE=192.168.1.10 npm run psn:listen
```

---

## Project files

Projects are saved as `.mposi` JSON files (**File → Save Project**). They contain:

- Calibration homography
- Tracker IDs, names, and colours
- NDI source name
- Network settings

---

## Troubleshooting

**CMake fails to find Qt6Multimedia**

- Install Qt Multimedia via Homebrew: `brew install qtmultimedia`
- Then re-run configure: `cmake -S . -B build`

**CMake fails to find the NDI SDK**

- Install “NDI SDK for Apple” (see above) and ensure it is present at `/Library/NDI SDK for Apple/`
- Then re-run configure: `cmake -S . -B build`

> Note: This project requires the NDI SDK to build (CMake will fail without it).

**NDI source not found**

- Ensure the NDI source (camera encoder) is on the same network segment.
- Click **Refresh** in the NDI source dialog to re-scan.
- Firewall: NDI uses TCP 5960 for discovery and UDP/TCP for streams.

**PSN not received by grandMA**

- Check the multicast IP and port match the grandMA PSN input plugin settings.
- Verify multicast routing on your network (try unicast mode as a workaround).
- Use Wireshark to confirm packets are leaving the machine on port 56565.

**Windows: "Qt6Multimedia.dll not found" when launching**

Run `windeployqt` as shown in the build steps above. This copies all required Qt DLLs and plugins into the `build\Release\` folder next to the exe.

**Windows: CMake fails to find OpenCV even with vcpkg**

Make sure both the Qt path and the vcpkg installed path are in `CMAKE_PREFIX_PATH`, separated by a semicolon:
```
-DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64;C:\vcpkg\installed\x64-windows"
```
Do not rely on the `%VCPKG_ROOT%` environment variable in PowerShell — use `$env:VCPKG_ROOT` or a literal path instead.

**Windows: NDI SDK not found by CMake**

The installer places the SDK at `C:\Program Files (x86)\NDI\NDI 6 SDK` (note the `(x86)` path). CMake searches this location automatically. If you installed it elsewhere, set the `NDI_SDK_DIR` environment variable to the SDK root before running cmake.

**Windows ARM64: "No CMAKE_CXX_COMPILER could be found" with `-A x64`**

The default MSVC toolset (14.34) bundled with some BuildTools installs lacks the ARM64→x64 cross-compiler. Add `-T version=14.41,host=ARM64` to select the newer toolset that includes it. Verify it is installed by checking that `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.41.xxxxx\bin\Hostarm64\x64\cl.exe` exists.

**High CPU usage**

- The app caps NDI decoding at 30 fps. If usage is still high, check whether the NDI
  source is sending an unusually high-resolution stream.
