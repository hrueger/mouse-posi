# Changelog

## v0.2.0 - 2026-05-29

### Added
- Added DeckLink support improvements.
- Added Linux build and release support.
- Added Unifi clickplane and PSN height support, scrolling, and sACN input.
- Added configurable dock layouts.
- Added a basic 3D stage view.
- Added a welcome screen.
- Added MVR import.
- Improved project loading and saving.

### Changed
- Replaced the Qt ZIP reader with libarchive.

### Fixed
- Fixed the Windows DeckLink build.

### CI
- Updated Linux builds to use a newer Qt version.
- Bundled Qt, OpenCV, and the Linux runtime dependency closure in Linux artifacts.
- Avoided recopying bundled Linux libraries.

## v0.1.2 - 2026-05-20

### Added
- Respected configured network interfaces for PSN and sessions.

## v0.1.1 - 2026-05-18

### Fixed
- Disabled macOS library validation to allow loading the DeckLink SDK.
- Removed Homebrew OpenCV rpaths and stripped Homebrew rpaths from macOS app bundles to prevent OMP double-load issues.
- Added caching for Homebrew downloads to improve build performance.

## v0.1.0 - 2026-05-16

### Added
- Added 3D calibration.

## v0.0.1 - 2026-05-15

### Added
- Added session management features and MainWindow UI updates.
- Added Qlementine theme support and layout improvements.
- Added the TrackerBar for tracker selection and fullscreen toggling.
- Added the Help menu and About dialog.
- Added webcam and DeckLink support.
- Added project loading and saving improvements, including error handling and station tracker persistence.
- Added Windows support and GitHub Actions workflows.
- Added macOS notarization, camera permissions, and entitlements handling.
- Added application rebranding and the version bump script.

### Changed
- Refactored the network panel to show relevant fields and interfaces.

### Fixed
- Fixed segfaults when closing the window.
- Swapped axes so they match grandMA.
- Ignored trackers when recalibrating.
- Fixed Windows build and installer issues, including Qt, OpenCV, MSVC, vcpkg, and relative path handling.
