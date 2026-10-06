cmake_minimum_required(VERSION 3.22)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static runtime: no VC++ redist needed
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
project(ChromaEqualizer VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ChromaEqualizer
  COMPANY_NAME "Himz DSP"
  PLUGIN_MANUFACTURER_CODE Himz
  PLUGIN_CODE Chrm
  FORMATS VST3
  VST3_CATEGORIES Fx EQ
  PRODUCT_NAME "Chroma-Equalizer"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ChromaEqualizer PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp)

target_compile_definitions(ChromaEqualizer PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ChromaEqualizer
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
