# mouse-posi

Follow-spot position tracking for live entertainment. An NDI camera above FOH streams
a view of the stage; operators click and hold on the video to output tracker positions
via **PosiStageNet (PSN v2)** UDP to a grandMA3 console. Multiple operators run the app
simultaneously — each owns one or more tracker IDs (keys **1–9**) and sees all other
operators' positions overlaid on the video.

## Requirements

| Dependency               | Version       | Install                                                                     |
| ------------------------ | ------------- | --------------------------------------------------------------------------- |
| macOS                    | 13+ (Ventura) | —                                                                           |
| Xcode Command Line Tools | any recent    | `xcode-select --install`                                                    |
| CMake                    | ≥ 3.25        | `brew install cmake`                                                        |
| Qt 6 (base)              | ≥ 6.4         | `brew install qtbase`                                                       |
| Qt SVG                   | ≥ 6.4         | `brew install qtsvg`                                                        |
| Qt Multimedia (webcam)   | ≥ 6.4         | `brew install qtmultimedia`                                                 |
| OpenCV                   | ≥ 4           | `brew install opencv`                                                       |
| NDI SDK for Apple        | 6.x           | [ndi.video](https://ndi.video/for-developers/ndi-sdk/) → _Download_ → macOS |

### NDI SDK installation

1. Download "NDI SDK for Apple" from ndi.video and run the installer.
2. The installer places the SDK at `/Library/NDI SDK for Apple/`.
3. CMake finds it automatically — no extra steps needed.

---

## Build

```bash
# Clone / enter the project
git clone <repo-url> mouse-posi && cd mouse-posi

# Install dependencies (Homebrew)
brew install cmake qtbase qtsvg qtmultimedia opencv

# Configure (first time only)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run
open build/mouse-posi.app
# or directly:
./build/mouse-posi.app/Contents/MacOS/mouse-posi
```

> **Tip:** If Qt or OpenCV are not on the default CMake search path, pass
> `-DCMAKE_PREFIX_PATH=/opt/homebrew` (Homebrew Apple Silicon) or
> `-DCMAKE_PREFIX_PATH=/usr/local` (Homebrew Intel).

> If you installed missing Qt components after a failed configure, clear the cache:
> `rm -f build/CMakeCache.txt` and re-run `cmake -S . -B build`.

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

**High CPU usage**

- The app caps NDI decoding at 30 fps. If usage is still high, check whether the NDI
  source is sending an unusually high-resolution stream.
