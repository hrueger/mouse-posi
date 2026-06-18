# Changelog

All notable changes to OnPoint are documented in this file.

## [0.3.0] - 2026-06-18

### Added
- MIDI controller support for input height in 2D stage view
- PSN origin alignment: configurable offset/rotation, origin markers in stage view, drag tool, and "Snap to MVR origin" button
- Camera modes: 2D Camera, 3D Stage (PSN), and 3D Stage (DMX) with GDTF pan/tilt support
- MVR fixture rendering in 3D stage view

### Changed
- Improved universe management with per-universe input/output routing
- Improved saving and loading
- Improved channel display and tracker assignment UI

### Fixed
- DeckLink SDI input not working

## [0.2.0]

### Added
- Welcome screen
- Basic 3D stage view
- MVR file import
- Configurable dock layouts
- Linux build and release support
- DeckLink capture support
- sACN input support
- PSN height from clickplane
- Scrolling in tracker views

### Changed
- Replaced Qt zip reader with libarchive for MVR/GDTF archive handling
- Improved loading and saving

## [0.1.2]

### Changed
- Network interface settings are now respected for PSN and session traffic

## [0.1.1]

### Fixed
- macOS: strip Homebrew rpaths from app bundle to prevent OpenMP double-load
- macOS: disabled library validation to allow loading DeckLink SDK

## [0.1.0]

### Added
- 3D calibration

## [0.0.1]

### Added
- Session management (host/join)
- Tracker bar for tracker selection and fullscreen toggle
- Qlementine theme and UI polish
- Network panel with interface selection
- Webcam video source support
- DeckLink video source support
- Project loading and saving with error handling
- About dialog
- macOS camera permission handling and notarization
- Windows support
- GitHub Actions CI for macOS, Windows, and Linux
