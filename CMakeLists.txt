cmake_minimum_required(VERSION 3.22)

# The build sets EMERSON_VERSION (1.0.<build number>); EMERSON compares it with the latest release to offer updates.
set(EMERSON_VERSION "1.0.0" CACHE STRING "Version of this build")
project(EMERSON VERSION ${EMERSON_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the .exe runs without installing the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(EMERSON_BUILD_APP   "Build the EMERSON application" ON)
option(EMERSON_BUILD_TESTS "Build the headless engine tests" OFF)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_DIR is given, otherwise fetch it.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ENGINE_SOURCES
    Source/Engine/Rack.cpp
    Source/Engine/RackState.cpp
    Source/Link/LinkHost.cpp)

set(COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(EMERSON_BUILD_APP)
    # -----------------------------------------------------------------------
    # The PEQUOD plugins run natively, from the same engine PEQUOD Host links in:
    # its Rust source, and the host's C++ side of it (PequodNative / PequodPanel),
    # come from a PEQUOD Host checkout (the workflow checks it out next to this repo).
    # -----------------------------------------------------------------------
    set(PEQUOD_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pequod-host" CACHE PATH "A PEQUOD Host checkout (for the PEQUOD engine)")
    if(NOT EXISTS "${PEQUOD_HOST_DIR}/Source/PequodNative.cpp")
        message(FATAL_ERROR "No PEQUOD Host checkout at ${PEQUOD_HOST_DIR}: clone ShallowMouse/PEQUOD-HOST there, or set PEQUOD_HOST_DIR")
    endif()

    if(WIN32)
        set(_pq_default "${PEQUOD_HOST_DIR}/engine/target/x86_64-pc-windows-msvc/release/pequod_engine.lib")
    else()
        set(_pq_default "${PEQUOD_HOST_DIR}/engine/target/release/libpequod_engine.a")
    endif()
    set(PEQUOD_ENGINE_LIB "${_pq_default}" CACHE FILEPATH "The built PEQUOD engine library")
    if(NOT EXISTS "${PEQUOD_ENGINE_LIB}")
        message(FATAL_ERROR "PEQUOD engine not built: see ${PEQUOD_HOST_DIR}/engine/README.md (cargo build --release -p pequod_engine)")
    endif()

    # the system libraries the engine needs, as rustc reported them (the workflow saves them next to the library)
    set(PEQUOD_ENGINE_NATIVE_LIBS)
    if(EXISTS "${PEQUOD_ENGINE_LIB}.libs")
        file(READ "${PEQUOD_ENGINE_LIB}.libs" _pq_libs)
        string(REGEX MATCHALL "[^ \t\r\n]+" _pq_tokens "${_pq_libs}")
        foreach(_t IN LISTS _pq_tokens)
            if(_t MATCHES "^(.+)\\.lib$")
                list(APPEND PEQUOD_ENGINE_NATIVE_LIBS "${CMAKE_MATCH_1}")
            elseif(_t MATCHES "^-l(.+)$")
                list(APPEND PEQUOD_ENGINE_NATIVE_LIBS "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES PEQUOD_ENGINE_NATIVE_LIBS)
        list(REMOVE_ITEM PEQUOD_ENGINE_NATIVE_LIBS c msvcrt libcmt ucrt libucrt vcruntime libvcruntime)
    elseif(WIN32)
        set(PEQUOD_ENGINE_NATIVE_LIBS kernel32 advapi32 bcrypt ntdll userenv ws2_32 ole32 oleaut32 shell32 user32 gdi32 opengl32 dwmapi uxtheme crypt32 secur32 propsys uuid)
    else()
        set(PEQUOD_ENGINE_NATIVE_LIBS xcb GL X11-xcb X11 pthread dl m rt util)
    endif()
    get_filename_component(PEQUOD_ENGINE_DIR "${PEQUOD_ENGINE_LIB}" DIRECTORY)

    juce_add_binary_data(EmersonFonts SOURCES
        Resources/Fonts/CourierPrime-Regular.ttf
        Resources/Fonts/CourierPrime-Bold.ttf
        Resources/Fonts/Roboto-Regular.ttf
        Resources/Fonts/Roboto-Bold.ttf
        Resources/Fonts/Ubuntu-Light.ttf
        Resources/Fonts/Hack-Regular.ttf)

    juce_add_gui_app(Emerson
        PRODUCT_NAME "EMERSON"
        COMPANY_NAME "PEQUOD"
        BUNDLE_ID    "com.pequod.emerson"
        ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon/EMERSON.png"
        ICON_SMALL   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon/EMERSON-small.png"
        VERSION      ${PROJECT_VERSION})

    juce_generate_juce_header(Emerson)

    target_sources(Emerson PRIVATE
        ${ENGINE_SOURCES}
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/RackPanel.cpp
        Source/TrackList.cpp
        Source/Plugins.cpp
        Source/RouteStatus.cpp
        Source/Updater.cpp
        "${PEQUOD_HOST_DIR}/Source/PequodNative.cpp"
        "${PEQUOD_HOST_DIR}/Source/PequodPanel.cpp")

    target_include_directories(Emerson PRIVATE "${PEQUOD_HOST_DIR}/Source")

    target_compile_definitions(Emerson PRIVATE ${COMMON_DEFS}
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Emerson,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Emerson,JUCE_VERSION>")

    target_link_directories(Emerson PRIVATE "${PEQUOD_ENGINE_DIR}")
    target_link_libraries(Emerson
        PRIVATE
            "${PEQUOD_ENGINE_LIB}"
            ${PEQUOD_ENGINE_NATIVE_LIBS}
            EmersonFonts
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Headless tests: the insert chains and latency alignment, saving and loading,
# Hawthorne Route's channel names, the studio list and the EMERSON Link.
# ---------------------------------------------------------------------------
if(EMERSON_BUILD_TESTS)
    juce_add_console_app(EmersonTests PRODUCT_NAME "EmersonTests")
    target_sources(EmersonTests PRIVATE ${ENGINE_SOURCES} Tests/EmersonTests.cpp)
    target_compile_definitions(EmersonTests PRIVATE ${COMMON_DEFS} JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0)
    target_link_libraries(EmersonTests PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags)
endif()
