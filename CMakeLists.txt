cmake_minimum_required(VERSION 3.22)

project(meridian VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is vendored in libs/JUCE (cloned at tag 8.0.14)
add_subdirectory(libs/JUCE)

# Family-shared components (design language, presets, state).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../jdm_shared" jdm_shared)

juce_add_plugin(meridian
    COMPANY_NAME "Equinox Audio"
    BUNDLE_ID com.equinoxaudio.meridian
    PLUGIN_MANUFACTURER_CODE Eeqa
    PLUGIN_CODE Mrdn
    FORMATS VST3 Standalone
    PRODUCT_NAME "Meridian"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics)

target_sources(meridian PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(meridian PRIVATE src)

target_compile_definitions(meridian PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(meridian
    PRIVATE
        jdm_shared
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP test harness (console app around the processor sources).
juce_add_console_app(meridian_tests PRODUCT_NAME "meridian_tests")

target_sources(meridian_tests PRIVATE
    test/DspTests.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(meridian_tests PRIVATE src)

target_compile_definitions(meridian_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(meridian_tests
    PRIVATE
        jdm_shared
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
