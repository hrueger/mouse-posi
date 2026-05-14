cmake_minimum_required(VERSION 3.25)
project(mouse-posi VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
find_package(OpenCV REQUIRED COMPONENTS core calib3d imgproc)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/VideoWidget.cpp
    src/NdiReceiver.cpp
    src/PsnSender.cpp
    src/PsnReceiver.cpp
    src/Calibration.cpp
    src/Project.cpp
    # New sidebar UI
    src/ui/CollapsibleSection.cpp
    src/ui/SidebarWidget.cpp
    src/ui/TrackersPanel.cpp
    src/ui/NdiPanel.cpp
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
    src/PsnSender.h
    src/PsnReceiver.h
    src/Calibration.h
    src/Project.h
    src/ui/CollapsibleSection.h
    src/ui/SidebarWidget.h
    src/ui/TrackersPanel.h
    src/ui/NdiPanel.h
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
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(mouse-posi PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    ${OpenCV_LIBS}
)

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
    message(WARNING "NDI SDK not found — building in stub mode (placeholder frames only)")
    target_compile_definitions(mouse-posi PRIVATE NDI_AVAILABLE=0)
endif()

if(APPLE)
    set_target_properties(mouse-posi PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Mouse Posi"
    )
    target_sources(mouse-posi PRIVATE src/DnsSdBridge.mm)
    set_source_files_properties(src/DnsSdBridge.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++")
    find_library(FOUNDATION Foundation REQUIRED)
    target_link_libraries(mouse-posi PRIVATE ${FOUNDATION})
endif()
