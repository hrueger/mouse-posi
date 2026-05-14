cmake_minimum_required(VERSION 3.25)
project(mouse-posi VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt discovery hints ────────────────────────────────────────────────────────
# Homebrew (Apple only); on Windows, set CMAKE_PREFIX_PATH to your Qt install.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH
        "/opt/homebrew/opt/qtbase"
        "/opt/homebrew/opt/qtsvg"
        "/opt/homebrew/opt/qtmultimedia")
    set(Qt6Svg_DIR        "/opt/homebrew/opt/qtsvg/lib/cmake/Qt6Svg"              CACHE PATH "")
    set(Qt6Multimedia_DIR "/opt/homebrew/opt/qtmultimedia/lib/cmake/Qt6Multimedia" CACHE PATH "")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Svg Multimedia)
find_package(OpenCV REQUIRED COMPONENTS core calib3d imgproc)

# Derive Qt install prefix — needed to locate auxiliary artifacts on Apple
# (static camera-permission plugin).
if(APPLE)
    set(_qt6_prefix "${Qt6_DIR}")
    get_filename_component(_qt6_prefix "${_qt6_prefix}" DIRECTORY) # .../lib/cmake
    get_filename_component(_qt6_prefix "${_qt6_prefix}" DIRECTORY) # .../lib
    get_filename_component(_qt6_prefix "${_qt6_prefix}" DIRECTORY) # .../<qt prefix>
endif()

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
    src/DnsSdBridge.h
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

# ── NDI SDK discovery ─────────────────────────────────────────────────────────
# Search order: project-local ndi/, then platform SDK locations.
if(WIN32)
    set(NDI_SEARCH_PATHS
        "${CMAKE_SOURCE_DIR}/ndi"
        "C:/Program Files/NDI/NDI 6 SDK"
        "C:/Program Files (x86)/NDI/NDI 6 SDK"
        "C:/Program Files/NDI/NDI Advanced SDK 6"
        "C:/Program Files (x86)/NDI/NDI Advanced SDK 6"
        "$ENV{NDI_SDK_DIR}"
    )
else()
    set(NDI_SEARCH_PATHS
        "${CMAKE_SOURCE_DIR}/ndi"
        "/Library/NDI SDK for Apple"
        "/usr/local"
    )
endif()

set(NDI_FOUND FALSE)
foreach(NDI_BASE ${NDI_SEARCH_PATHS})
    if(NOT NDI_BASE STREQUAL "" AND EXISTS "${NDI_BASE}/include/Processing.NDI.Lib.h")
        set(NDI_INCLUDE_DIR "${NDI_BASE}/include")
        if(WIN32)
            foreach(_arch x64 x86)
                if(EXISTS "${NDI_BASE}/lib/${_arch}/Processing.NDI.Lib.${_arch}.lib")
                    set(NDI_LIB "${NDI_BASE}/lib/${_arch}/Processing.NDI.Lib.${_arch}.lib")
                    if(EXISTS "${NDI_BASE}/bin/${_arch}/Processing.NDI.Lib.${_arch}.dll")
                        set(NDI_DLL "${NDI_BASE}/bin/${_arch}/Processing.NDI.Lib.${_arch}.dll")
                    endif()
                    break()
                endif()
            endforeach()
        else()
            if(EXISTS "${NDI_BASE}/lib/macOS/libndi.dylib")
                set(NDI_LIB "${NDI_BASE}/lib/macOS/libndi.dylib")
            elseif(EXISTS "${NDI_BASE}/lib/libndi.dylib")
                set(NDI_LIB "${NDI_BASE}/lib/libndi.dylib")
            endif()
        endif()
        if(NDI_LIB)
            set(NDI_FOUND TRUE)
            message(STATUS "Found NDI SDK: ${NDI_BASE}")
            break()
        endif()
    endif()
endforeach()

# macOS fallback: runtime lib in /usr/local/lib, headers from SDK package
if(NOT NDI_FOUND AND APPLE AND EXISTS "/usr/local/lib/libndi.dylib")
    set(NDI_LIB "/usr/local/lib/libndi.dylib")
    if(EXISTS "/Library/NDI SDK for Apple/include/Processing.NDI.Lib.h")
        set(NDI_INCLUDE_DIR "/Library/NDI SDK for Apple/include")
        set(NDI_FOUND TRUE)
        message(STATUS "Found NDI SDK (runtime in /usr/local/lib, headers in NDI SDK for Apple)")
    endif()
endif()

target_include_directories(mouse-posi PRIVATE
    src
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
    if(APPLE)
        # Copy dylib into the .app bundle so it's self-contained
        add_custom_command(TARGET mouse-posi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NDI_LIB}"
                "$<TARGET_FILE_DIR:mouse-posi>/libndi.dylib")
    elseif(WIN32 AND DEFINED NDI_DLL AND EXISTS "${NDI_DLL}")
        # Copy runtime DLL next to the executable
        add_custom_command(TARGET mouse-posi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NDI_DLL}"
                "$<TARGET_FILE_DIR:mouse-posi>/")
    endif()
else()
    if(WIN32)
        message(FATAL_ERROR
            "NDI SDK not found.\n"
            "Install the NDI 6 SDK for Windows from https://ndi.video/download-ndi-sdk/ "
            "or place the SDK directory under ${CMAKE_SOURCE_DIR}/ndi/.\n"
            "You can also set the NDI_SDK_DIR environment variable to the SDK root.")
    else()
        message(FATAL_ERROR
            "NDI SDK not found. "
            "Install 'NDI SDK for Apple' (6.x) so CMake can find "
            "Processing.NDI.Lib.h and libndi.dylib.")
    endif()
endif()

# ── Apple-specific ────────────────────────────────────────────────────────────
if(APPLE)
    # Homebrew's qtbase ships permissions as static libs; import the camera one
    # so Qt can handle QCameraPermission at runtime.
    set(_qt_camera_perm_lib
        "${_qt6_prefix}/share/qt/plugins/permissions/libqdarwincamerapermission.a")
    if(EXISTS "${_qt_camera_perm_lib}")
        target_link_libraries(mouse-posi PRIVATE "${_qt_camera_perm_lib}")
        target_compile_definitions(mouse-posi PRIVATE HAVE_QT_DARWIN_CAMERA_PERMISSION_PLUGIN=1)
        message(STATUS "Linking Qt camera permission static plugin: ${_qt_camera_perm_lib}")
    else()
        message(WARNING
            "Qt camera permission plugin not found at ${_qt_camera_perm_lib} "
            "— webcam permission prompt may not work")
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

    find_library(FOUNDATION    Foundation    REQUIRED)
    find_library(AVFOUNDATION  AVFoundation  REQUIRED)
    find_library(COREMEDIA     CoreMedia     REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)
    target_link_libraries(mouse-posi PRIVATE
        ${FOUNDATION} ${AVFOUNDATION} ${COREMEDIA} ${COREFOUNDATION})

    # DeckLink SDK (macOS): headers bundled in third_party/decklink/include/
    set(_decklink_macos_inc "${CMAKE_SOURCE_DIR}/third_party/decklink/include")
    if(NOT EXISTS "${_decklink_macos_inc}/DeckLinkAPI.h")
        message(FATAL_ERROR
            "DeckLink SDK headers not found at ${_decklink_macos_inc}/.\n"
            "Download the DeckLink SDK from "
            "https://www.blackmagicdesign.com/developer/product/capture-and-playback "
            "and place the Mac/include/ contents there.")
    endif()
    target_include_directories(mouse-posi PRIVATE "${_decklink_macos_inc}")
    target_sources(mouse-posi PRIVATE "${_decklink_macos_inc}/DeckLinkAPIDispatch.cpp")
    target_compile_definitions(mouse-posi PRIVATE DECKLINK_AVAILABLE=1)
endif()

# ── Windows-specific ──────────────────────────────────────────────────────────
if(WIN32)
    # Use the Windows GUI subsystem (no console window)
    set_target_properties(mouse-posi PROPERTIES WIN32_EXECUTABLE TRUE)

    target_link_libraries(mouse-posi PRIVATE ws2_32)

    # DNS-SD (mDNS) for session discovery via Apple Bonjour for Windows.
    # Download the Bonjour SDK from https://developer.apple.com/bonjour/ or
    # set BONJOUR_SDK_HOME to its install directory.
    find_path(BONJOUR_INCLUDE_DIR dns_sd.h
        PATHS
            "C:/Program Files/Bonjour SDK/Include"
            "$ENV{BONJOUR_SDK_HOME}/Include"
        NO_DEFAULT_PATH)
    find_library(BONJOUR_LIB dnssd
        PATHS
            "C:/Program Files/Bonjour SDK/Lib/x64"
            "C:/Program Files/Bonjour SDK/Lib/Win32"
            "$ENV{BONJOUR_SDK_HOME}/Lib/x64"
            "$ENV{BONJOUR_SDK_HOME}/Lib/Win32"
        NO_DEFAULT_PATH)

    if(NOT BONJOUR_INCLUDE_DIR OR NOT BONJOUR_LIB)
        message(FATAL_ERROR
            "Apple Bonjour SDK not found.\n"
            "Download the Bonjour SDK for Windows from "
            "https://developer.apple.com/download/all/?q=Bonjour+SDK+for+Windows "
            "and install it, or set BONJOUR_SDK_HOME to its root directory.")
    endif()
    target_sources(mouse-posi PRIVATE src/DnsSdBridge_win.cpp)
    target_include_directories(mouse-posi PRIVATE "${BONJOUR_INCLUDE_DIR}")
    target_link_libraries(mouse-posi PRIVATE "${BONJOUR_LIB}")
    target_compile_definitions(mouse-posi PRIVATE DNSSD_AVAILABLE=1)
    message(STATUS "Found Apple Bonjour SDK — session discovery enabled")

    # DeckLink SDK (Windows): pre-generated headers in third_party/decklink/Win/include/
    # DeckLinkAPI.h and DeckLinkAPI_i.c were generated from DeckLinkAPI.idl via midl.exe.
    set(_decklink_win_inc "${CMAKE_SOURCE_DIR}/third_party/decklink/Win/include")
    if(NOT EXISTS "${_decklink_win_inc}/DeckLinkAPI.h")
        message(FATAL_ERROR
            "DeckLink SDK for Windows not found at ${_decklink_win_inc}/.\n"
            "Download the DeckLink SDK from "
            "https://www.blackmagicdesign.com/developer/product/capture-and-playback\n"
            "Place the Win/include/ contents there, then run midl.exe on DeckLinkAPI.idl "
            "to generate DeckLinkAPI.h and DeckLinkAPI_i.c.")
    endif()
    target_include_directories(mouse-posi PRIVATE "${_decklink_win_inc}")
    # DeckLinkAPI_i.c provides COM CLSIDs/IIDs; compile as C
    target_sources(mouse-posi PRIVATE "${_decklink_win_inc}/DeckLinkAPI_i.c")
    set_source_files_properties("${_decklink_win_inc}/DeckLinkAPI_i.c" PROPERTIES LANGUAGE C)
    target_compile_definitions(mouse-posi PRIVATE DECKLINK_AVAILABLE=1)
    target_link_libraries(mouse-posi PRIVATE Ole32 OleAut32)
endif()
