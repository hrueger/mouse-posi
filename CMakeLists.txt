cmake_minimum_required(VERSION 3.25)
project(mouse-posi VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase" "/opt/homebrew/opt/qtsvg" "/opt/homebrew/opt/qtmultimedia")
set(Qt6Svg_DIR "/opt/homebrew/opt/qtsvg/lib/cmake/Qt6Svg" CACHE PATH "")
set(Qt6Multimedia_DIR "/opt/homebrew/opt/qtmultimedia/lib/cmake/Qt6Multimedia" CACHE PATH "")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Svg Multimedia)
find_package(OpenCV REQUIRED COMPONENTS core calib3d imgproc)

# Homebrew Qt splits modules across formulae; derive the Qt prefix from Qt6_DIR
# so we can reference auxiliary artifacts (e.g. static permission plugins).
set(_qt6_prefix "${Qt6_DIR}")
get_filename_component(_qt6_prefix "${_qt6_prefix}" DIRECTORY) # .../lib/cmake
get_filename_component(_qt6_prefix "${_qt6_prefix}" DIRECTORY) # .../lib
get_filename_component(_qt6_prefix "${_qt6_prefix}" DIRECTORY) # .../<qt prefix>

include(FetchContent)
FetchContent_Declare(
    qlementine
    GIT_REPOSITORY https://github.com/oclero/qlementine.git
    GIT_TAG v1.4.2
)
FetchContent_MakeAvailable(qlementine)

set(SOURCES
    src/resources.qrc
    src/main.cpp
    src/MainWindow.cpp
    src/VideoWidget.cpp
    src/NdiReceiver.cpp
    src/WebcamCapture.cpp
    src/DeckLinkCapture.cpp
    src/PsnSender.cpp
    src/PsnReceiver.cpp
    src/Calibration.cpp
    src/Project.cpp
    # New sidebar UI
    src/ui/CollapsibleSection.cpp
    src/ui/SidebarWidget.cpp
    src/ui/TrackersPanel.cpp
    src/ui/TrackerBar.cpp
    src/ui/StreamSourcePanel.cpp
    src/ui/NetworkSettingsPanel.cpp
    src/ui/StatsPanel.cpp
    src/ui/CalibrationPanel.cpp
    src/ui/SessionPanel.cpp
    # Session management
    src/SessionManager.cpp
)

set(HEADERS
    src/MainWindow.h
    src/VideoWidget.h
    src/NdiReceiver.h
    src/WebcamCapture.h
    src/DeckLinkCapture.h
    src/PsnSender.h
    src/PsnReceiver.h
    src/Calibration.h
    src/Project.h
    src/ui/CollapsibleSection.h
    src/ui/SidebarWidget.h
    src/ui/TrackersPanel.h
    src/ui/TrackerBar.h
    src/ui/StreamSourcePanel.h
    src/ui/NetworkSettingsPanel.h
    src/ui/StatsPanel.h
    src/ui/CalibrationPanel.h
    src/ui/SessionPanel.h
    src/SessionManager.h
)

add_executable(mouse-posi ${SOURCES} ${HEADERS})

# ── NDI SDK discovery ──────────────────────────────────────────────────────
# Search order: project-local ndi/, then system locations
set(NDI_SEARCH_PATHS
    "${CMAKE_SOURCE_DIR}/ndi"
    "/Library/NDI SDK for Apple"
    "/usr/local"
)

set(NDI_FOUND FALSE)
foreach(NDI_BASE ${NDI_SEARCH_PATHS})
    if(EXISTS "${NDI_BASE}/include/Processing.NDI.Lib.h")
        set(NDI_INCLUDE_DIR "${NDI_BASE}/include")
        # Pick the right lib subdir
        if(EXISTS "${NDI_BASE}/lib/macOS/libndi.dylib")
            set(NDI_LIB "${NDI_BASE}/lib/macOS/libndi.dylib")
        elseif(EXISTS "${NDI_BASE}/lib/libndi.dylib")
            set(NDI_LIB "${NDI_BASE}/lib/libndi.dylib")
        elseif(EXISTS "${NDI_BASE}/lib/libndi.dylib")
            set(NDI_LIB "${NDI_BASE}/lib/libndi.dylib")
        endif()
        if(NDI_LIB)
            set(NDI_FOUND TRUE)
            message(STATUS "Found NDI SDK: ${NDI_BASE}")
            break()
        endif()
    endif()
endforeach()

# Also check /usr/local/lib directly (NDI runtime install puts it there)
if(NOT NDI_FOUND AND EXISTS "/usr/local/lib/libndi.dylib")
    set(NDI_LIB "/usr/local/lib/libndi.dylib")
    # Header must be somewhere — check the Apple SDK path
    if(EXISTS "/Library/NDI SDK for Apple/include/Processing.NDI.Lib.h")
        set(NDI_INCLUDE_DIR "/Library/NDI SDK for Apple/include")
        set(NDI_FOUND TRUE)
        message(STATUS "Found NDI SDK (runtime in /usr/local/lib, headers in NDI SDK for Apple)")
    endif()
endif()

target_include_directories(mouse-posi PRIVATE
    src
    third_party/decklink/include
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(mouse-posi PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::Multimedia
    ${OpenCV_LIBS}
    qlementine
)

target_compile_definitions(mouse-posi PRIVATE WEBCAM_AVAILABLE=1)

if(NDI_FOUND)
    target_include_directories(mouse-posi PRIVATE "${NDI_INCLUDE_DIR}")
    target_link_libraries(mouse-posi PRIVATE "${NDI_LIB}")
    target_compile_definitions(mouse-posi PRIVATE NDI_AVAILABLE=1)
    # Copy dylib into the .app bundle so it's self-contained
    add_custom_command(TARGET mouse-posi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NDI_LIB}"
            "$<TARGET_FILE_DIR:mouse-posi>/libndi.dylib")
else()
    message(FATAL_ERROR "NDI SDK not found. Install 'NDI SDK for Apple' (6.x) so CMake can find Processing.NDI.Lib.h and libndi.dylib.")
endif()

if(APPLE)
    # Homebrew's qtbase ships permissions as static libs; import the camera one
    # so Qt can handle QCameraPermission at runtime.
    set(_qt_camera_perm_lib "${_qt6_prefix}/share/qt/plugins/permissions/libqdarwincamerapermission.a")
    if(EXISTS "${_qt_camera_perm_lib}")
        target_link_libraries(mouse-posi PRIVATE "${_qt_camera_perm_lib}")
        target_compile_definitions(mouse-posi PRIVATE HAVE_QT_DARWIN_CAMERA_PERMISSION_PLUGIN=1)
        message(STATUS "Linking Qt camera permission static plugin: ${_qt_camera_perm_lib}")
    else()
        message(WARNING "Qt camera permission plugin lib not found at ${_qt_camera_perm_lib} — webcam permission prompt may not work")
    endif()

    set_target_properties(mouse-posi PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Mouse Posi"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hrueger.mouse-posi"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
    )
    target_sources(mouse-posi PRIVATE src/DnsSdBridge.mm)
    set_source_files_properties(src/DnsSdBridge.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++")
    find_library(FOUNDATION Foundation REQUIRED)
    find_library(AVFOUNDATION AVFoundation REQUIRED)
    find_library(COREMEDIA CoreMedia REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)

    # DeckLink SDK: we build against the headers and link via the SDK dispatch loader.
    target_sources(mouse-posi PRIVATE third_party/decklink/include/DeckLinkAPIDispatch.cpp)
    target_compile_definitions(mouse-posi PRIVATE DECKLINK_AVAILABLE=1)

    target_link_libraries(mouse-posi PRIVATE ${FOUNDATION} ${AVFOUNDATION} ${COREMEDIA} ${COREFOUNDATION})
endif()
