cmake_minimum_required(VERSION 3.22)
project(Frequency VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: либо локальная копия (-DFREQUENCY_JUCE_DIR=C:/dev/JUCE), либо закреплённый тег с GitHub.
set(FREQUENCY_JUCE_DIR "" CACHE PATH "Путь к локальному JUCE (необязательно)")
if(FREQUENCY_JUCE_DIR)
    add_subdirectory(${FREQUENCY_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Frequency
    COMPANY_NAME "FrequencyAudio"
    PRODUCT_NAME "FREQUENCY"
    PLUGIN_MANUFACTURER_CODE Frqa
    PLUGIN_CODE Frq1
    FORMATS VST3
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)   # установка - отдельным скриптом

target_sources(Frequency PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/TunerPanel.cpp
    Source/UI/StationStrip.cpp
    Source/Engine/RadioEngine.cpp
    Source/Stations/StationPrep.cpp
    Source/Stations/StationManager.cpp)

target_include_directories(Frequency PRIVATE Source)

target_compile_definitions(Frequency PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Frequency
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)

# Юнит-тесты DSP-ядра (чистый C++, без JUCE): ctest -C Release --output-on-failure
enable_testing()
add_executable(frequency_dsp_tests tests/test_dsp.cpp)
add_executable(frequency_engine_tests tests/test_engine.cpp
    Source/Engine/RadioEngine.cpp Source/Stations/StationPrep.cpp)
add_executable(frequency_ui_model_tests tests/test_ui_model.cpp)
foreach(t frequency_dsp_tests frequency_engine_tests frequency_ui_model_tests)
    target_include_directories(${t} PRIVATE Source)
    target_compile_features(${t} PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endforeach()
add_test(NAME dsp    COMMAND frequency_dsp_tests)
add_test(NAME engine COMMAND frequency_engine_tests)
add_test(NAME ui_model COMMAND frequency_ui_model_tests)
