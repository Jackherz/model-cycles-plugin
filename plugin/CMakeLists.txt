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
# Which render: build-stf (the Syntakt engines and the track filter, NOTES.md
# step 17), else build-st (the Syntakt engines, step 16), else build (stock
# 1.13), whichever has been made. Its engine state must match:
# ~/Library/Application Support/CyclesEmu/state.
if(NOT DEFINED MC_BUILD)
    if(EXISTS ${AOT}/build-sttw/render.c)
        set(MC_BUILD build-sttw)       # + SP TWINSHOT (NOTES.md step 38)
    elseif(EXISTS ${AOT}/build-stf/render.c)
        set(MC_BUILD build-stf)
    elseif(EXISTS ${AOT}/build-st/render.c)
        set(MC_BUILD build-st)
    else()
        set(MC_BUILD build)
    endif()
endif()
set(MCB ${AOT}/${MC_BUILD})
if(NOT EXISTS ${MCB}/render.c)
    message(FATAL_ERROR "${MCB}/render.c is missing: run ../aot/build.sh (or translate.py --out)")
endif()
message(STATUS "Engine render: ${MCB}")

# The engine: the generated render (big: one function, built once) and its runtime.
add_library(mcengine STATIC ${MCB}/render.c ${AOT}/runtime.c ${AOT}/engine.c)
target_include_directories(mcengine PUBLIC ${AOT})
target_compile_options(mcengine PRIVATE -O2 -Wno-parentheses-equality)
# profile-guided when ../aot/pgo.sh has made the profile (about -35% per block)
if(EXISTS ${MCB}/render.profdata)
    target_compile_options(mcengine PRIVATE -fprofile-instr-use=${MCB}/render.profdata
        -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
    set_property(SOURCE ${MCB}/render.c APPEND PROPERTY OBJECT_DEPENDS ${MCB}/render.profdata)
else()
    message(WARNING "No ${MCB}/render.profdata: run ../aot/pgo.sh STATE ${MC_BUILD} for a faster engine")
endif()
set_target_properties(mcengine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The alternative delay and reverb engines: Schwung audio effects (MIT),
# vendored unchanged in ThirdParty/ with their licences, behind small
# wrappers (ThirdParty/wrap, fxwrap.h)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty)
add_library(cyemfx STATIC ${TP}/wrap/cloudseed_wrap.c ${TP}/wrap/psxverb_wrap.c ${TP}/wrap/spacecho_wrap.c
    ${TP}/wrap/chompi_wrap.cpp)
target_include_directories(cyemfx PUBLIC ${TP}/wrap
    PRIVATE ${TP}/munchi-delay ${TP}/munchi-delay/tempo ${TP}/munchi-delay/tempo/shim ${TP}/munchi-delay/daisysp)
target_compile_options(cyemfx PRIVATE -O2 -w)
target_compile_features(cyemfx PRIVATE cxx_std_17)
set_target_properties(cyemfx PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

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

target_sources(CyclesEmu PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Retro.cpp Source/Help.cpp Source/Icons.cpp Source/Presets.cpp Source/Randomise.cpp Source/FxEngines.cpp Source/TwinSamples.cpp Source/Fanfare.cpp)
target_compile_definitions(CyclesEmu PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(CyclesEmu
    PRIVATE mcengine cyemfx juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# CyclesVoice: the same sources as one sound played polyphonically on the
# six engine tracks (Product::Voice, PluginProcessor.h).
juce_add_plugin(CyclesVoice
    COMPANY_NAME "Personal"
    BUNDLE_ID "personal.cyclesvoice"
    PLUGIN_MANUFACTURER_CODE Pers
    PLUGIN_CODE Cyvo
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "CyclesVoice"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)
target_sources(CyclesVoice PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Retro.cpp Source/Help.cpp Source/Icons.cpp Source/Presets.cpp Source/Randomise.cpp Source/FxEngines.cpp Source/TwinSamples.cpp Source/Fanfare.cpp)
target_compile_definitions(CyclesVoice PUBLIC
    CYCLES_PRODUCT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(CyclesVoice
    PRIVATE mcengine cyemfx juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# CyclesFX: the same sources as an audio effect: the host's audio into the
# engine's delay and reverb sends (Product::Fx; the render's --hook points).
juce_add_plugin(CyclesFX
    COMPANY_NAME "Personal"
    BUNDLE_ID "personal.cyclesfx"
    PLUGIN_MANUFACTURER_CODE Pers
    PLUGIN_CODE Cyfx
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "CyclesFX"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)
target_sources(CyclesFX PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Retro.cpp Source/Help.cpp Source/Icons.cpp Source/Presets.cpp Source/Randomise.cpp Source/FxEngines.cpp Source/TwinSamples.cpp Source/Fanfare.cpp)
target_compile_definitions(CyclesFX PUBLIC
    CYCLES_PRODUCT=2 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(CyclesFX
    PRIVATE mcengine cyemfx juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# Host the built VST3 like a DAW: identity with the engine, callback timings.
juce_add_console_app(CyclesEmuHostTest PRODUCT_NAME "CyclesEmuHostTest")
target_sources(CyclesEmuHostTest PRIVATE Test/HostTest.cpp)
target_compile_definitions(CyclesEmuHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CyclesEmuHostTest PRIVATE mcengine cyemfx
    juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
    "-framework CoreAudioKit")

# Render the editor's pages to PNG, to check the layout without a host.
juce_add_console_app(CyclesEmuEditorShot PRODUCT_NAME "CyclesEmuEditorShot")
target_sources(CyclesEmuEditorShot PRIVATE Test/EditorShot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Retro.cpp Source/Help.cpp Source/Icons.cpp Source/Presets.cpp Source/Randomise.cpp Source/FxEngines.cpp Source/TwinSamples.cpp Source/Fanfare.cpp)
target_compile_definitions(CyclesEmuEditorShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CyclesEmuEditorShot PRIVATE mcengine cyemfx juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The resampler against JUCE's WindowedSincInterpolator: quality and time.
juce_add_console_app(CyclesEmuResamplerTest PRODUCT_NAME "CyclesEmuResamplerTest")
target_sources(CyclesEmuResamplerTest PRIVATE Test/ResamplerTest.cpp)
target_compile_definitions(CyclesEmuResamplerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CyclesEmuResamplerTest PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)

# The installed plugins loaded as a host loads them (VST3, as Ableton Live),
# run like Live runs them: a crash here is a crash in a DAW.
juce_add_console_app(CyclesEmuLoadTest PRODUCT_NAME "CyclesEmuLoadTest")
target_sources(CyclesEmuLoadTest PRIVATE Test/LoadTest.cpp)
target_compile_definitions(CyclesEmuLoadTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CyclesEmuLoadTest PRIVATE juce::juce_audio_processors juce::juce_gui_basics
    juce::juce_recommended_config_flags)
