cmake_minimum_required(VERSION 3.22)
project(DiscoSauceSplitt VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# the front-facing flowmotion / Disco Sauce artwork is baked into the plugin
juce_add_binary_data(DiscoAssets SOURCES Assets/bg.jpg)

juce_add_plugin(DiscoSauceSplitt
    COMPANY_NAME "flowmotionlb"
    PLUGIN_MANUFACTURER_CODE Flmo
    PLUGIN_CODE Dskf
    FORMATS VST3 Standalone
    PRODUCT_NAME "Disco Sauce Key Finder"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    )

juce_generate_juce_header(DiscoSauceSplitt)

target_sources(DiscoSauceSplitt PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DiscoSauceSplitt PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(DiscoSauceSplitt
    PRIVATE
        DiscoAssets
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags)
