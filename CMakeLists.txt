cmake_minimum_required(VERSION 3.22)
project(AxellMaster VERSION 4.0.1)

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

# 2.0: the streaming codec check encodes and decodes the master with the reference Opus and Vorbis libraries (both
# BSD-3-Clause; Opus with royalty-free patent licences). Fetched from the Xiph.Org release tarballs (pinned by hash)
# and built as static, position-independent libraries.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_Declare(ogg URL https://github.com/xiph/ogg/releases/download/v1.3.6/libogg-1.3.6.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  URL_HASH SHA256=83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638)
FetchContent_Declare(vorbis URL https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  URL_HASH SHA256=0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab)
FetchContent_Declare(opus URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1)
FetchContent_GetProperties(ogg)
if(NOT ogg_POPULATED)
  FetchContent_Populate(ogg)
endif()
FetchContent_GetProperties(vorbis)
if(NOT vorbis_POPULATED)
  FetchContent_Populate(vorbis)
endif()
file(WRITE ${CMAKE_BINARY_DIR}/ogg-config/ogg/config_types.h
  "#ifndef __CONFIG_TYPES_H__\n#define __CONFIG_TYPES_H__\n#include <stdint.h>\ntypedef int16_t ogg_int16_t;\ntypedef uint16_t ogg_uint16_t;\ntypedef int32_t ogg_int32_t;\ntypedef uint32_t ogg_uint32_t;\ntypedef int64_t ogg_int64_t;\ntypedef uint64_t ogg_uint64_t;\n#endif\n")
add_library(axm_ogg STATIC ${ogg_SOURCE_DIR}/src/bitwise.c ${ogg_SOURCE_DIR}/src/framing.c)
target_include_directories(axm_ogg PUBLIC ${ogg_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/ogg-config)
set(VORBIS_SRC bitrate block codebook envelope floor0 floor1 info lookup lpc lsp mapping0 mdct psy registry res0
  sharedbook smallft synthesis vorbisenc window analysis)
list(TRANSFORM VORBIS_SRC PREPEND ${vorbis_SOURCE_DIR}/lib/)
list(TRANSFORM VORBIS_SRC APPEND .c)
add_library(axm_vorbis STATIC ${VORBIS_SRC})
target_include_directories(axm_vorbis PUBLIC ${vorbis_SOURCE_DIR}/include PRIVATE ${vorbis_SOURCE_DIR}/lib)
target_link_libraries(axm_vorbis PUBLIC axm_ogg m)
target_compile_options(axm_vorbis PRIVATE -w)
target_compile_options(axm_ogg PRIVATE -w)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(opus)
add_library(axm_codecs INTERFACE)
target_link_libraries(axm_codecs INTERFACE opus axm_vorbis)

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
# JUCE_USE_OGGVORBIS=0: JUCE carries its own libvorbis copy; the codec check links the reference one (one definition)
set(MASTER_DEFS JUCE_WEB_BROWSER=0 AXELL_THEME_HUE=150 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_OGGVORBIS=0)
target_sources(AxellMaster PRIVATE ${MASTER_SOURCES})
target_compile_definitions(AxellMaster PUBLIC ${MASTER_DEFS})
axell_core_add_gui(AxellMaster)
target_link_libraries(AxellMaster PRIVATE juce::juce_audio_utils axm_codecs
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
target_link_libraries(MasterDspTests PRIVATE axell_core_dsp axm_codecs pthread)
target_compile_options(MasterDspTests PRIVATE -Wall -Wextra -Wshadow -Wno-mismatched-new-delete $<$<CONFIG:Release>:-O3>)
master_sanitize(MasterDspTests)
add_test(NAME dsp_suite COMMAND MasterDspTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# plugin suite + GUI snapshots (JUCE)
foreach(tool MasterPluginTests MasterGuiSnapshot)
  juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
  target_compile_definitions(${tool} PRIVATE ${MASTER_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Axell Master")
  target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags axm_codecs)
  axell_core_add_gui(${tool})
  master_sanitize(${tool})
endforeach()
target_sources(MasterPluginTests PRIVATE tests/PluginTests.cpp ${MASTER_SOURCES})
target_sources(MasterGuiSnapshot PRIVATE tests/GuiSnapshot.cpp ${MASTER_SOURCES})
add_test(NAME plugin_suite COMMAND MasterPluginTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# offline renderer (the engine + codec check exactly as they ship): the real-mix study and the codec acceptance test
add_executable(MasterRender tools/MasterRender.cpp)
target_include_directories(MasterRender PRIVATE Source)
target_link_libraries(MasterRender PRIVATE axell_core_dsp axm_codecs pthread)
target_compile_options(MasterRender PRIVATE -O3)

# independent black-box audit host (from the core)
juce_add_console_app(MasterAuditHost PRODUCT_NAME "MasterAuditHost")
target_sources(MasterAuditHost PRIVATE ${AXELL_CORE_DIR}/tools/AuditHost.cpp)
target_compile_definitions(MasterAuditHost PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(MasterAuditHost PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
