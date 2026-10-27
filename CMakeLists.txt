cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(ParametricFuzz VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ParametricFuzz
    COMPANY_NAME "EJ Audio"
    BUNDLE_ID "com.ejaudio.parametricfuzz"
    PLUGIN_MANUFACTURER_CODE Ejau
    PLUGIN_CODE Pfz1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Parametric Fuzz"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion EQ
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(PfAssets SOURCES
    assets/panel.jpg
    assets/knob.png
    assets/trim.png
    assets/bandwidth.png
    assets/footswitch_pressed.png
    assets/footswitch_released.png
    assets/led_green_on.png
    assets/led_green_off.png
    assets/led_blue_on.png
    assets/led_blue_off.png)

target_sources(ParametricFuzz PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(ParametricFuzz PRIVATE src)

target_compile_definitions(ParametricFuzz PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ParametricFuzz
    PRIVATE
        PfAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless tool that renders the editor to PNG (used by CI to check the skin)
juce_add_console_app(SkinSnapshot PRODUCT_NAME "SkinSnapshot")
target_sources(SkinSnapshot PRIVATE tools/snapshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(SkinSnapshot PRIVATE src)
target_compile_definitions(SkinSnapshot PRIVATE
    JucePlugin_Name="Parametric Fuzz"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(SkinSnapshot PRIVATE PfAssets juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)
