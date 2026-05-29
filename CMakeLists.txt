cmake_minimum_required(VERSION 3.25)
project(onpoint VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt discovery hints ────────────────────────────────────────────────────────
# Homebrew (Apple only); on Windows, set CMAKE_PREFIX_PATH to your Qt install.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH
        "/opt/homebrew/opt/qt"
        "/opt/homebrew/opt/qtbase"
        "/opt/homebrew/opt/qtsvg"
        "/opt/homebrew/opt/qtmultimedia"
        "/opt/homebrew/opt/qtopengl")
    set(Qt6Svg_DIR        "/opt/homebrew/opt/qtsvg/lib/cmake/Qt6Svg"              CACHE PATH "")
    set(Qt6Multimedia_DIR "/opt/homebrew/opt/qtmultimedia/lib/cmake/Qt6Multimedia" CACHE PATH "")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Svg Multimedia OpenGL OpenGLWidgets Xml)
find_package(Qt6CorePrivate REQUIRED)
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

# ── libMVRgdtf (MVR/GDTF file format) ────────────────────────────────────────
set(DONT_USE_XERCES_AS_XMLLIB ON  CACHE BOOL "" FORCE)
set(BUILD_MVR_XCHANGE         OFF CACHE BOOL "" FORCE)
set(UNITTEST                  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libmvrgdtf
    GIT_REPOSITORY https://github.com/mvrdevelopment/libMVRgdtf.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libmvrgdtf)


set(SOURCES
    src/resources.qrc
    src/main.cpp
    src/MvrImporter.cpp
    src/cgltf_impl.cpp
    src/MainWindow.cpp
    src/VideoWidget.cpp
    src/NdiReceiver.cpp
    src/WebcamCapture.cpp
    src/DeckLinkCapture.cpp
    src/PsnSender.cpp
    src/PsnReceiver.cpp
    src/SacnReceiver.cpp
    src/Calibration.cpp
    src/Project.cpp
    src/ProjectWorker.cpp
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
    src/ui/Stage3DView.cpp
    src/ui/Stage3DPanel.cpp
    src/ui/StageItemsPanel.cpp
    src/ui/MvrImportDialog.cpp
    src/ui/StagePropertiesPanel.cpp
    src/ui/WelcomeScreen.cpp
    # Session management
    src/SessionManager.cpp
)

set(HEADERS
    src/MainWindow.h
    src/MvrImporter.h
    src/VideoWidget.h
    src/NdiReceiver.h
    src/WebcamCapture.h
    src/DeckLinkCapture.h
    src/PsnSender.h
    src/PsnReceiver.h
    src/SacnReceiver.h
    src/Calibration.h
    src/Project.h
    src/ProjectWorker.h
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
    src/ui/Stage3DView.h
    src/ui/Stage3DPanel.h
    src/ui/StageItemsPanel.h
    src/ui/MvrImportDialog.h
    src/ui/StagePropertiesPanel.h
    src/ui/WelcomeScreen.h
    src/SessionManager.h
)

if(APPLE)
    list(APPEND SOURCES src/assets/onpoint.icns)
    set_source_files_properties(src/assets/onpoint.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()
if(WIN32)
    list(APPEND SOURCES src/onpoint.rc)
endif()

add_executable(onpoint ${SOURCES} ${HEADERS})

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
elseif(APPLE)
    set(NDI_SEARCH_PATHS
        "${CMAKE_SOURCE_DIR}/ndi"
        "/Library/NDI SDK for Apple"
        "/usr/local"
        "$ENV{NDI_SDK_DIR}"
    )
else()
    set(NDI_SEARCH_PATHS
        "${CMAKE_SOURCE_DIR}/ndi"
        "/opt/ndi"
        "/usr/local"
        "/usr"
        "$ENV{NDI_SDK_DIR}"
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
        elseif(APPLE)
            if(EXISTS "${NDI_BASE}/lib/macOS/libndi.dylib")
                set(NDI_LIB "${NDI_BASE}/lib/macOS/libndi.dylib")
            elseif(EXISTS "${NDI_BASE}/lib/libndi.dylib")
                set(NDI_LIB "${NDI_BASE}/lib/libndi.dylib")
            endif()
        else()
            foreach(_lib_path
                "${NDI_BASE}/lib/x86_64-linux-gnu/libndi.so"
                "${NDI_BASE}/lib/x64/libndi.so"
                "${NDI_BASE}/lib/libndi.so"
                "${NDI_BASE}/lib64/libndi.so")
                if(EXISTS "${_lib_path}")
                    set(NDI_LIB "${_lib_path}")
                    break()
                endif()
            endforeach()
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

target_sources(onpoint PRIVATE third_party/libe131/e131.c)
set_source_files_properties(third_party/libe131/e131.c PROPERTIES LANGUAGE C)

target_include_directories(onpoint PRIVATE
    src
    third_party/libe131
    third_party/cgltf
    ${OpenCV_INCLUDE_DIRS}
    ${libmvrgdtf_SOURCE_DIR}/src
)

target_link_libraries(onpoint PRIVATE
    Qt6::Core
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::Multimedia
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Xml
    ${OpenCV_LIBS}
    qlementine
    MvrGdtf
)

target_compile_definitions(onpoint PRIVATE WEBCAM_AVAILABLE=1)

if(NDI_FOUND)
    target_include_directories(onpoint PRIVATE "${NDI_INCLUDE_DIR}")
    target_link_libraries(onpoint PRIVATE "${NDI_LIB}")
    target_compile_definitions(onpoint PRIVATE NDI_AVAILABLE=1)
    if(APPLE)
        # Copy dylib into the .app bundle so it's self-contained
        add_custom_command(TARGET onpoint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NDI_LIB}"
                "$<TARGET_FILE_DIR:onpoint>/libndi.dylib")
    elseif(WIN32 AND DEFINED NDI_DLL AND EXISTS "${NDI_DLL}")
        # Copy runtime DLL next to the executable
        add_custom_command(TARGET onpoint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NDI_DLL}"
                "$<TARGET_FILE_DIR:onpoint>/")
    endif()
else()
    if(WIN32)
        message(FATAL_ERROR
            "NDI SDK not found.\n"
            "Install the NDI 6 SDK for Windows from https://ndi.video/download-ndi-sdk/ "
            "or place the SDK directory under ${CMAKE_SOURCE_DIR}/ndi/.\n"
            "You can also set the NDI_SDK_DIR environment variable to the SDK root.")
    elseif(APPLE)
        message(FATAL_ERROR
            "NDI SDK not found. "
            "Install 'NDI SDK for Apple' (6.x) so CMake can find "
            "Processing.NDI.Lib.h and libndi.dylib.")
    else()
        message(FATAL_ERROR
            "NDI SDK not found. Install the NDI SDK for Linux so CMake can find "
            "Processing.NDI.Lib.h and libndi.so, place it under ${CMAKE_SOURCE_DIR}/ndi/, "
            "or set NDI_SDK_DIR to the SDK root.")
    endif()
endif()

# ── Apple-specific ────────────────────────────────────────────────────────────
if(APPLE)
    # Load the camera permission plugin via its cmake target (Qt 6.5+).
    # The target carries INTERFACE_LINK_OPTIONS with -Wl,-u,_QDarwinCameraPermissionRequest
    # which prevents the macOS dead-stripper from removing the ObjC class that
    # drives the AVFoundation permission request.  That flag is gated on the
    # consuming target having _qt_has_QDarwinCameraPermissionPlugin_usage_description
    # set (normally done by qt_add_executable); we set it here manually because
    # we know our Info.plist.in contains NSCameraUsageDescription.
    get_filename_component(_qt6_cmake_parent "${Qt6_DIR}" DIRECTORY)
    find_package(Qt6QDarwinCameraPermissionPlugin QUIET
        PATHS "${_qt6_cmake_parent}/Qt6Core"
        NO_DEFAULT_PATH)
    if(TARGET Qt6::QDarwinCameraPermissionPlugin)
        set_target_properties(onpoint PROPERTIES
            _qt_has_QDarwinCameraPermissionPlugin_usage_description TRUE)
        target_link_libraries(onpoint PRIVATE Qt6::QDarwinCameraPermissionPlugin)
        target_compile_definitions(onpoint PRIVATE HAVE_QT_DARWIN_CAMERA_PERMISSION_PLUGIN=1)
        message(STATUS "Found Qt Darwin camera permission plugin")
    else()
        message(WARNING
            "Qt camera permission plugin not found in ${_qt6_cmake_parent}/Qt6Core "
            "— webcam permission prompt may not work")
    endif()

    set_target_properties(onpoint PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "OnPoint"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hrueger.onpoint"
        MACOSX_BUNDLE_ICON_FILE "onpoint"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
    )

    target_sources(onpoint PRIVATE src/DnsSdBridge.mm)
    set_source_files_properties(src/DnsSdBridge.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++")

    find_library(FOUNDATION    Foundation    REQUIRED)
    find_library(AVFOUNDATION  AVFoundation  REQUIRED)
    find_library(COREMEDIA     CoreMedia     REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)
    target_link_libraries(onpoint PRIVATE
        ${FOUNDATION} ${AVFOUNDATION} ${COREMEDIA} ${COREFOUNDATION})
    # MvrGdtf uses CoreFoundation for TXString (UUID, string ops)
    target_link_libraries(onpoint PRIVATE ${COREFOUNDATION})

    # DeckLink SDK (macOS): headers bundled in third_party/decklink/include/
    set(_decklink_macos_inc "${CMAKE_SOURCE_DIR}/third_party/decklink/include")
    if(NOT EXISTS "${_decklink_macos_inc}/DeckLinkAPI.h")
        message(FATAL_ERROR
            "DeckLink SDK headers not found at ${_decklink_macos_inc}/.\n"
            "Download the DeckLink SDK from "
            "https://www.blackmagicdesign.com/developer/product/capture-and-playback "
            "and place the Mac/include/ contents there.")
    endif()
    target_include_directories(onpoint PRIVATE "${_decklink_macos_inc}")
    target_sources(onpoint PRIVATE "${_decklink_macos_inc}/DeckLinkAPIDispatch.cpp")
    target_compile_definitions(onpoint PRIVATE DECKLINK_AVAILABLE=1)

endif()

# ── Linux-specific ────────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    find_path(DNSSD_INCLUDE_DIR dns_sd.h)
    find_library(DNSSD_LIB dns_sd)
    if(NOT DNSSD_INCLUDE_DIR OR NOT DNSSD_LIB)
        message(FATAL_ERROR
            "DNS-SD compatibility library not found. Install libavahi-compat-libdnssd-dev "
            "(Debian/Ubuntu) or the equivalent package for your distribution.")
    endif()

    target_sources(onpoint PRIVATE src/DnsSdBridge_win.cpp)
    target_include_directories(onpoint PRIVATE "${DNSSD_INCLUDE_DIR}")
    target_link_libraries(onpoint PRIVATE "${DNSSD_LIB}")
    target_compile_definitions(onpoint PRIVATE DNSSD_AVAILABLE=1)
    message(STATUS "Found DNS-SD compatibility library — session discovery enabled")

    install(TARGETS onpoint RUNTIME DESTINATION bin)
endif()

# ── Windows-specific ──────────────────────────────────────────────────────────
if(WIN32)
    # Use the Windows GUI subsystem (no console window)
    set_target_properties(onpoint PROPERTIES WIN32_EXECUTABLE TRUE)

    target_link_libraries(onpoint PRIVATE ws2_32)

    # DNS-SD (mDNS) for session discovery via Apple Bonjour for Windows.
    # Download the Bonjour SDK from https://developer.apple.com/bonjour/ or
    # set BONJOUR_SDK_HOME to its install directory.
    find_path(BONJOUR_INCLUDE_DIR dns_sd.h
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/bonjour/Include"
            "C:/Program Files/Bonjour SDK/Include"
            "$ENV{BONJOUR_SDK_HOME}/Include"
        NO_DEFAULT_PATH)
    find_library(BONJOUR_LIB dnssd
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/bonjour/Lib/x64"
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
    target_sources(onpoint PRIVATE src/DnsSdBridge_win.cpp)
    target_include_directories(onpoint PRIVATE "${BONJOUR_INCLUDE_DIR}")
    target_link_libraries(onpoint PRIVATE "${BONJOUR_LIB}")
    target_compile_definitions(onpoint PRIVATE DNSSD_AVAILABLE=1)
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
    target_include_directories(onpoint PRIVATE "${_decklink_win_inc}")
    # DeckLinkAPI_i.c provides COM CLSIDs/IIDs; compile as C
    target_sources(onpoint PRIVATE "${_decklink_win_inc}/DeckLinkAPI_i.c")
    set_source_files_properties("${_decklink_win_inc}/DeckLinkAPI_i.c" PROPERTIES LANGUAGE C)
    target_compile_definitions(onpoint PRIVATE DECKLINK_AVAILABLE=1)
    target_link_libraries(onpoint PRIVATE Ole32 OleAut32)
endif()
