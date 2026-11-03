cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) unless overridden on the command line.
# Must be set before project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Boba VERSION 2.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BOBA_INSTALL_AFTER_BUILD "Copy VST3/AU into ~/Library/Audio/Plug-Ins after building" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Boba
    COMPANY_NAME "Boba"
    PLUGIN_MANUFACTURER_CODE Bbcm
    PLUGIN_CODE Boba
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Boba"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${BOBA_INSTALL_AFTER_BUILD})

# Artwork shown in the UI (embedded in the plugin binary)
juce_add_binary_data(BobaAssets SOURCES Source/Assets/boba_art.jpg)

target_sources(Boba PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/BobaCore.h)

target_compile_definitions(Boba PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Boba
    PRIVATE BobaAssets
            juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)

# Plain C++ DSP tests (no JUCE): ctest -R core
enable_testing()
add_executable(boba_core_tests tests/test_core.cpp)
add_test(NAME core COMMAND boba_core_tests)
