cmake_minimum_required(VERSION 3.22)

project(SMSAnvilSampler VERSION 0.0.6 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANVILSAMPLER_BUILD_VST3 "Also build a VST3 bundle" OFF)
option(ANVILSAMPLER_BUILD_CLAP "Also build a CLAP bundle" OFF)
set(ANVILSAMPLER_HELP_REF "main" CACHE STRING
  "Git ref used for Online Help (main for development builds, release tag for releases)")
if(NOT ANVILSAMPLER_HELP_REF MATCHES "^(main|SMS-AnvilSampler-v[0-9]+\\.[0-9]+\\.[0-9]+)$")
  message(FATAL_ERROR "ANVILSAMPLER_HELP_REF must be main or an SMS-AnvilSampler-vX.Y.Z tag")
endif()
if(NOT ANVILSAMPLER_HELP_REF STREQUAL "main" AND
   NOT ANVILSAMPLER_HELP_REF STREQUAL "SMS-AnvilSampler-v${PROJECT_VERSION}")
  message(FATAL_ERROR "ANVILSAMPLER_HELP_REF must match the CMake project version")
endif()

set(SMS_DPF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/DPF")

if(NOT EXISTS "${SMS_DPF_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "DPF is missing. Run: git submodule update --init --recursive")
endif()

if(UNIX AND NOT APPLE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ANVILSAMPLER_DBUS REQUIRED dbus-1)
endif()

set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SMS_DPF_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/DPF"
  EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Common-UI"
  "${CMAKE_CURRENT_BINARY_DIR}/Common-UI" EXCLUDE_FROM_ALL)

add_library(anvilsampler_core STATIC
  src/core/PadClipboard.cpp
  src/core/SamplerEngine.cpp)

target_include_directories(anvilsampler_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src")

set_target_properties(anvilsampler_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(anvilsampler_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
  target_compile_options(anvilsampler_core PRIVATE /W4 /permissive-)
endif()

set(ANVILSAMPLER_PLUGIN_FORMATS lv2)
if(ANVILSAMPLER_BUILD_CLAP)
  list(APPEND ANVILSAMPLER_PLUGIN_FORMATS clap)
endif()

set(ANVILSAMPLER_DSP_SOURCES
  ../Common-Src/Audio/WavCodec.cpp
  src/plugin/MidichopperPlugin.cpp
  src/plugin/PadFileActions.cpp
  src/plugin/StateCodec.cpp)
set(ANVILSAMPLER_ARTWORK_DIR "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ANVILSAMPLER_ARTWORK_DIR}")
add_custom_command(
  OUTPUT
    "${ANVILSAMPLER_ARTWORK_DIR}/anvilsampler_artwork.cpp"
    "${ANVILSAMPLER_ARTWORK_DIR}/anvilsampler_artwork.hpp"
  COMMAND "${Python3_EXECUTABLE}" "${SMS_DPF_ROOT}/utils/res2c.py"
    anvilsampler_artwork "${CMAKE_CURRENT_SOURCE_DIR}/res"
    "${ANVILSAMPLER_ARTWORK_DIR}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/res/anvil.png"
    "${SMS_DPF_ROOT}/utils/res2c.py"
  VERBATIM)
set(ANVILSAMPLER_UI_SOURCES
  src/ui/MidichopperUI.cpp
  src/ui/MidichopperView.cpp
  "${ANVILSAMPLER_ARTWORK_DIR}/anvilsampler_artwork.cpp"
  "${ANVILSAMPLER_ARTWORK_DIR}/anvilsampler_artwork.hpp")

dpf_add_plugin(SMS-AnvilSampler
  TARGETS ${ANVILSAMPLER_PLUGIN_FORMATS}
  UI_TYPE opengl
  USE_FILE_BROWSER
  FILES_DSP ${ANVILSAMPLER_DSP_SOURCES}
  FILES_UI ${ANVILSAMPLER_UI_SOURCES})

# Only the VST3 variant needs same-process UI access. Keep LV2's separate UI
# binary and its existing host feature requirements unchanged.
if(ANVILSAMPLER_BUILD_VST3)
  dpf_add_plugin(SMS-AnvilSampler-VST3
    TARGETS vst3
    UI_TYPE opengl
    USE_FILE_BROWSER
    FILES_DSP ${ANVILSAMPLER_DSP_SOURCES}
    FILES_UI ${ANVILSAMPLER_UI_SOURCES})
  target_compile_definitions(SMS-AnvilSampler-VST3 INTERFACE
    ANVILSAMPLER_VST3_UI_BRIDGE=1)

  dpf__determine_vst3_package_architecture(anvilsampler_vst3_arch)
  if(APPLE)
    set(anvilsampler_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilSampler.vst3/Contents/MacOS/$<0:>")
    set(INFO_PLIST_PROJECT_NAME "SMS-Anvil Sampler")
    configure_file("${SMS_DPF_ROOT}/utils/plugin.bundle/Contents/Info.plist"
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilSampler.vst3/Contents/Info.plist" @ONLY)
    file(COPY "${SMS_DPF_ROOT}/utils/plugin.bundle/Contents/PkgInfo"
      DESTINATION "${PROJECT_BINARY_DIR}/bin/SMS-AnvilSampler.vst3/Contents")
  elseif(WIN32)
    set(anvilsampler_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilSampler.vst3/Contents/${anvilsampler_vst3_arch}-win/$<0:>")
  else()
    set(anvilsampler_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilSampler.vst3/Contents/${anvilsampler_vst3_arch}-linux/$<0:>")
  endif()
  set_target_properties(SMS-AnvilSampler-VST3-vst3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${anvilsampler_vst3_binary_dir}"
    OUTPUT_NAME "SMS-AnvilSampler")
endif()

# DPF currently passes disabled optional DGL features through generator
# expressions. CMake 4 evaluates those expressions as true while configuring
# the DGL target, so explicitly remove the web view this UI does not use.
foreach(property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
  get_target_property(dgl_definitions dgl-opengl "${property}")
  if(dgl_definitions)
    list(REMOVE_ITEM dgl_definitions DGL_USE_WEB_VIEW)
    set_target_properties(dgl-opengl PROPERTIES "${property}" "${dgl_definitions}")
  endif()
endforeach()

set(anvilsampler_targets SMS-AnvilSampler)
if(ANVILSAMPLER_BUILD_VST3)
  list(APPEND anvilsampler_targets SMS-AnvilSampler-VST3)
endif()
foreach(anvilsampler_target IN LISTS anvilsampler_targets)
  target_include_directories(${anvilsampler_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
  target_link_libraries(${anvilsampler_target} PUBLIC sms_common_ui)
  target_link_libraries(${anvilsampler_target}-dsp PRIVATE anvilsampler_core)
  target_include_directories(${anvilsampler_target}-ui PRIVATE
    "${ANVILSAMPLER_ARTWORK_DIR}")
  target_compile_definitions(${anvilsampler_target}-dsp PRIVATE
    ANVILSAMPLER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ANVILSAMPLER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ANVILSAMPLER_VERSION_PATCH=${PROJECT_VERSION_PATCH})
  target_compile_definitions(${anvilsampler_target}-ui PRIVATE
    ANVILSAMPLER_VERSION_STRING="${PROJECT_VERSION}"
    ANVILSAMPLER_HELP_REF="${ANVILSAMPLER_HELP_REF}")
endforeach()

if(BUILD_TESTING)
  find_package(Threads REQUIRED)

  add_executable(anvilsampler_core_tests tests/core_tests.cpp)
  target_link_libraries(anvilsampler_core_tests PRIVATE anvilsampler_core)
  add_test(NAME sampler-core COMMAND anvilsampler_core_tests)

  add_executable(anvilsampler_state_codec_tests
    tests/state_codec_tests.cpp
    src/plugin/StateCodec.cpp)
  target_include_directories(anvilsampler_state_codec_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  add_test(NAME state-codec COMMAND anvilsampler_state_codec_tests)

  add_executable(anvilsampler_ui_geometry_tests tests/ui_geometry_tests.cpp)
  target_include_directories(anvilsampler_ui_geometry_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
  target_link_libraries(anvilsampler_ui_geometry_tests PRIVATE sms_common_ui)
  add_test(NAME ui-geometry COMMAND anvilsampler_ui_geometry_tests)

  add_executable(anvilsampler_ui_message_bus_tests tests/ui_message_bus_tests.cpp)
  target_include_directories(anvilsampler_ui_message_bus_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(anvilsampler_ui_message_bus_tests PRIVATE Threads::Threads)
  add_test(NAME ui-message-bus COMMAND anvilsampler_ui_message_bus_tests)

  add_executable(anvilsampler_wav_codec_tests
    tests/wav_codec_tests.cpp
    ../Common-Src/Audio/WavCodec.cpp
    src/plugin/PadFileActions.cpp)
  target_include_directories(anvilsampler_wav_codec_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(anvilsampler_wav_codec_tests PRIVATE Threads::Threads)
  add_test(NAME wav-codec COMMAND anvilsampler_wav_codec_tests)
endif()

install(
  DIRECTORY "${PROJECT_BINARY_DIR}/bin/SMS-AnvilSampler.lv2"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2")
