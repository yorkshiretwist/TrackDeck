cmake_minimum_required(VERSION 3.22)

project(TrackDeck VERSION 0.1.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Optional ASIO support (Windows only, Standalone build only — see the
# README's "ASIO support on Windows" section for full setup instructions).
# VST3 hosts pick their own audio driver independently of this plugin, so
# this only matters if you run the Standalone app directly and want to
# select an ASIO device from its own audio settings dialog.
#
# Steinberg's ASIO SDK license forbids redistributing the SDK itself, so it
# is NOT included in this repository — download it yourself and point CMake
# at the extracted folder via TRACKDECK_ASIO_SDK_PATH.
# ---------------------------------------------------------------------------
option(TRACKDECK_ENABLE_ASIO "Enable ASIO audio device support in the Standalone build (Windows only, requires the ASIO SDK)" OFF)
set(TRACKDECK_ASIO_SDK_PATH "" CACHE PATH "Path to the extracted Steinberg ASIO SDK (the folder that directly contains 'common', 'host', 'driver', ...)")

# ---------------------------------------------------------------------------
# This project depends on the JUCE framework. Either:
#   1) Clone JUCE next to this CMakeLists.txt:
#        git clone --branch 7.0.12 --depth 1 https://github.com/juce-framework/JUCE.git
#      so the folder layout is:
#        TrackDeck/
#          CMakeLists.txt
#          JUCE/            <-- cloned here
#          Source/
#   2) Or point add_subdirectory() below at wherever you already keep JUCE.
# ---------------------------------------------------------------------------
add_subdirectory(JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(TrackDeck
    COMPANY_NAME                "Chris Taylor"
    PLUGIN_MANUFACTURER_CODE    Chta
    PLUGIN_CODE                 Tdck
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "TrackDeck"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Generator Tools
)

clap_juce_extensions_plugin(
    TARGET                      TrackDeck
    CLAP_ID                     "com.christaylor.trackdeck"
    CLAP_FEATURES               audio-effect utility
    CLAP_DESCRIPTION            "A simple audio plugin for playing multiple audio files, for example synced backing track stems"
)

juce_generate_juce_header(TrackDeck)

target_sources(TrackDeck
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(TrackDeck
    PUBLIC
        JUCE_ALSA=1
        JUCE_DIRECTSOUND=1
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(TrackDeck
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Wire up ASIO if it was requested. This only makes sense on Windows and
# only affects the Standalone app's own audio device options (see the
# comment near the top of this file) — it does not change anything about
# the VST3 build, which never opens an audio device itself.
# ---------------------------------------------------------------------------
if (TRACKDECK_ENABLE_ASIO)
    if (NOT WIN32)
        message(WARNING "TRACKDECK_ENABLE_ASIO was set but this isn't a Windows build — ignoring it.")
    elseif (NOT TRACKDECK_ASIO_SDK_PATH)
        message(FATAL_ERROR
            "TRACKDECK_ENABLE_ASIO is ON but TRACKDECK_ASIO_SDK_PATH is not set. "
            "Download the ASIO SDK from Steinberg and pass its path, e.g.\n"
            "  cmake -B build -DTRACKDECK_ENABLE_ASIO=ON -DTRACKDECK_ASIO_SDK_PATH=\"C:/SDKs/ASIOSDK2.3.3\"\n"
            "See the README's \"ASIO support on Windows\" section for details.")
    elseif (NOT EXISTS "${TRACKDECK_ASIO_SDK_PATH}/common/iasiodrv.h")
        message(FATAL_ERROR
            "TRACKDECK_ASIO_SDK_PATH (\"${TRACKDECK_ASIO_SDK_PATH}\") doesn't look like a valid "
            "ASIO SDK folder — expected to find common/iasiodrv.h inside it. Point it at the folder "
            "that directly contains the SDK's 'common', 'host', and 'driver' subfolders.")
    else()
        message(STATUS "ASIO support enabled. SDK: ${TRACKDECK_ASIO_SDK_PATH}")

        target_compile_definitions(TrackDeck PUBLIC JUCE_ASIO=1)
        target_include_directories(TrackDeck PUBLIC "${TRACKDECK_ASIO_SDK_PATH}/common")
    endif()
endif()
