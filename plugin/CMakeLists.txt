# CyclesEmu: a Model:Cycles instrument on the AOT-compiled firmware render.
#
# Personal use. The generated render (../aot/build/render.c) is derived from
# Elektron's firmware: it is built here, never committed or distributed, and
# the engine state the plugin loads at run time lives outside the bundle
# (~/Library/Application Support/CyclesEmu/state).
#
#   ../aot/build.sh            # once: regenerates ../aot/build/render.c
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
cmake_minimum_required(VERSION 3.22)
project(CyclesEmu VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "")

include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

set(AOT ${CMAKE_CURRENT_SOURCE_DIR}/../aot)
if(NOT EXISTS ${AOT}/build/render.c)
    message(FATAL_ERROR "Run ../aot/build.sh first: ${AOT}/build/render.c is missing")
endif()

# The engine: the generated render (big: one function, built once) and its runtime.
add_library(mcengine STATIC ${AOT}/build/render.c ${AOT}/runtime.c ${AOT}/engine.c)
target_include_directories(mcengine PUBLIC ${AOT})
target_compile_options(mcengine PRIVATE -O2 -Wno-parentheses-equality)
# profile-guided when ../aot/pgo.sh has made the profile (about -35% per block)
if(EXISTS ${AOT}/build/render.profdata)
    target_compile_options(mcengine PRIVATE -fprofile-instr-use=${AOT}/build/render.profdata
        -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
    set_property(SOURCE ${AOT}/build/render.c APPEND PROPERTY OBJECT_DEPENDS ${AOT}/build/render.profdata)
else()
    message(WARNING "No ${AOT}/build/render.profdata: run ../aot/pgo.sh for a faster engine")
endif()
set_target_properties(mcengine PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(CyclesEmu
    COMPANY_NAME "Personal"
    BUNDLE_ID "personal.cyclesemu"
    PLUGIN_MANUFACTURER_CODE Pers
    PLUGIN_CODE Cyem
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "CyclesEmu"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(CyclesEmu PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Retro.cpp Source/Help.cpp Source/Icons.cpp)
target_compile_definitions(CyclesEmu PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(CyclesEmu
    PRIVATE mcengine juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# Host the built VST3 like a DAW: identity with the engine, callback timings.
juce_add_console_app(CyclesEmuHostTest PRODUCT_NAME "CyclesEmuHostTest")
target_sources(CyclesEmuHostTest PRIVATE Test/HostTest.cpp)
target_compile_definitions(CyclesEmuHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CyclesEmuHostTest PRIVATE mcengine
    juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
    "-framework CoreAudioKit")

# Render the editor's pages to PNG, to check the layout without a host.
juce_add_console_app(CyclesEmuEditorShot PRODUCT_NAME "CyclesEmuEditorShot")
target_sources(CyclesEmuEditorShot PRIVATE Test/EditorShot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Retro.cpp Source/Help.cpp Source/Icons.cpp)
target_compile_definitions(CyclesEmuEditorShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CyclesEmuEditorShot PRIVATE mcengine juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The resampler against JUCE's WindowedSincInterpolator: quality and time.
juce_add_console_app(CyclesEmuResamplerTest PRODUCT_NAME "CyclesEmuResamplerTest")
target_sources(CyclesEmuResamplerTest PRIVATE Test/ResamplerTest.cpp)
target_compile_definitions(CyclesEmuResamplerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CyclesEmuResamplerTest PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
