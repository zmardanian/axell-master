cmake_minimum_required(VERSION 3.22)
project(AxellMaster VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.15
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Axell core (shared DSP + look): next to this project, or vendored into external/axell-core for releases
set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/external/axell-core" CACHE PATH "Axell core")
if(NOT EXISTS "${AXELL_CORE_DIR}/CMakeLists.txt")
  set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../axell-core" CACHE PATH "Axell core" FORCE)
endif()
add_subdirectory(${AXELL_CORE_DIR} axell-core)

juce_add_plugin(AxellMaster
  COMPANY_NAME              "Axell Yobag"
  COMPANY_EMAIL             "axellpetre@protonmail.com"
  BUNDLE_ID                 com.axellyobag.axellmaster
  PLUGIN_MANUFACTURER_CODE  Axyb
  PLUGIN_CODE               Mst1
  FORMATS                   VST3
  PRODUCT_NAME              "Axell Master"
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD   FALSE
  VST3_CATEGORIES           Fx Dynamics Mastering)

set(MASTER_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(MASTER_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_sources(AxellMaster PRIVATE ${MASTER_SOURCES})
target_compile_definitions(AxellMaster PUBLIC ${MASTER_DEFS})
axell_core_add_gui(AxellMaster)
target_link_libraries(AxellMaster PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(MASTER_SANITIZE "AddressSanitizer + UBSan for the test programs" OFF)
function(master_sanitize target)
  if(MASTER_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# DSP suite (JUCE-free: the engine headers exactly as they ship)
enable_testing()
add_executable(MasterDspTests tests/DspTests.cpp)
target_include_directories(MasterDspTests PRIVATE Source)
target_link_libraries(MasterDspTests PRIVATE axell_core_dsp)
target_compile_options(MasterDspTests PRIVATE -Wall -Wextra -Wshadow -Wno-mismatched-new-delete $<$<CONFIG:Release>:-O3>)
master_sanitize(MasterDspTests)
add_test(NAME dsp_suite COMMAND MasterDspTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# plugin suite + GUI snapshots (JUCE)
foreach(tool MasterPluginTests MasterGuiSnapshot)
  juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
  target_compile_definitions(${tool} PRIVATE ${MASTER_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Axell Master")
  target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  axell_core_add_gui(${tool})
  master_sanitize(${tool})
endforeach()
target_sources(MasterPluginTests PRIVATE tests/PluginTests.cpp ${MASTER_SOURCES})
target_sources(MasterGuiSnapshot PRIVATE tests/GuiSnapshot.cpp ${MASTER_SOURCES})
add_test(NAME plugin_suite COMMAND MasterPluginTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# independent black-box audit host (from the core)
juce_add_console_app(MasterAuditHost PRODUCT_NAME "MasterAuditHost")
target_sources(MasterAuditHost PRIVATE ${AXELL_CORE_DIR}/tools/AuditHost.cpp)
target_compile_definitions(MasterAuditHost PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(MasterAuditHost PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
