cmake_minimum_required(VERSION 3.22)
project(AetherMicro VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
option(AETHER_BUILD_PLUGINS "Build JUCE VST3 and standalone applications" ON)
option(AETHER_BUILD_TESTS "Build behavioral tests" ON)
set(JUCE_PATH "" CACHE PATH "Existing JUCE 8.0.12 source checkout")
add_library(aether_dsp STATIC libs/aether-dsp/src/Engine.cpp)
target_include_directories(aether_dsp PUBLIC libs/aether-dsp/include)
if(MSVC)
  target_compile_options(aether_dsp PRIVATE /W4 /permissive- /fp:precise)
else()
  target_compile_options(aether_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(AETHER_BUILD_TESTS)
  enable_testing()
  add_executable(AetherDspTests tests/DspTests.cpp)
  target_link_libraries(AetherDspTests PRIVATE aether_dsp)
  add_test(NAME aether_dsp COMMAND AetherDspTests)
endif()
if(NOT AETHER_BUILD_PLUGINS)
  return()
endif()
if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
  add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
function(aether_plugin target product synth code)
  juce_add_plugin(${target}
    COMPANY_NAME "Aether Audio" COMPANY_WEBSITE "https://github.com/"
    BUNDLE_ID "audio.aether.${target}" PLUGIN_MANUFACTURER_CODE Aeth
    PLUGIN_CODE ${code} PRODUCT_NAME "${product}"
    IS_SYNTH ${synth} NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE FORMATS VST3 Standalone
    VST3_CATEGORIES $<IF:$<BOOL:${synth}>,Instrument,Fx> Tools)
  target_sources(${target} PRIVATE
    Source/Parameters.cpp Source/PresetManager.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_include_directories(${target} PUBLIC Source)
  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_USE_WIN_WEBVIEW2=0 JUCE_ASIO=0)
  target_link_libraries(${target} PUBLIC aether_dsp juce::juce_audio_utils juce::juce_dsp
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /bigobj)
  endif()
endfunction()
aether_plugin(AetherMicro "AETHER Micro" FALSE Aemf)
aether_plugin(AetherMicroSynth "AETHER Micro Synth" TRUE Aems)
if(AETHER_BUILD_TESTS)
  juce_add_console_app(AetherPluginTests PRODUCT_NAME "Aether Plugin Tests")
  target_sources(AetherPluginTests PRIVATE tests/PluginTests.cpp)
  target_link_libraries(AetherPluginTests PRIVATE AetherMicro)
  target_compile_definitions(AetherPluginTests PRIVATE AETHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME aether_plugin COMMAND AetherPluginTests)
endif()
