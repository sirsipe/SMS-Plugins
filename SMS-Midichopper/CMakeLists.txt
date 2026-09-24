cmake_minimum_required(VERSION 3.22)

project(SMSMidichopper VERSION 0.0.5 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MIDICHOPPER_BUILD_VST3 "Also build a VST3 bundle" OFF)
option(MIDICHOPPER_BUILD_CLAP "Also build a CLAP bundle" OFF)

set(SMS_DPF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/DPF")

if(NOT EXISTS "${SMS_DPF_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "DPF is missing. Run: git submodule update --init --recursive")
endif()

if(UNIX AND NOT APPLE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(MIDICHOPPER_DBUS REQUIRED dbus-1)
endif()

set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SMS_DPF_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/DPF"
  EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Common-UI"
  "${CMAKE_CURRENT_BINARY_DIR}/Common-UI" EXCLUDE_FROM_ALL)

add_library(midichopper_core STATIC
  src/core/PadClipboard.cpp
  src/core/SamplerEngine.cpp)

target_include_directories(midichopper_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src")

set_target_properties(midichopper_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(midichopper_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
  target_compile_options(midichopper_core PRIVATE /W4 /permissive-)
endif()

set(MIDICHOPPER_PLUGIN_FORMATS lv2)
if(MIDICHOPPER_BUILD_CLAP)
  list(APPEND MIDICHOPPER_PLUGIN_FORMATS clap)
endif()

set(MIDICHOPPER_DSP_SOURCES
  ../Common-Src/Audio/WavCodec.cpp
  src/plugin/MidichopperPlugin.cpp
  src/plugin/PadFileActions.cpp
  src/plugin/StateCodec.cpp)
set(MIDICHOPPER_UI_SOURCES
  src/ui/MidichopperUI.cpp
  src/ui/MidichopperView.cpp)

dpf_add_plugin(SMS-Midichopper
  TARGETS ${MIDICHOPPER_PLUGIN_FORMATS}
  UI_TYPE opengl
  USE_FILE_BROWSER
  FILES_DSP ${MIDICHOPPER_DSP_SOURCES}
  FILES_UI ${MIDICHOPPER_UI_SOURCES})

# Only the VST3 variant needs same-process UI access. Keep LV2's separate UI
# binary and its existing host feature requirements unchanged.
if(MIDICHOPPER_BUILD_VST3)
  dpf_add_plugin(SMS-Midichopper-VST3
    TARGETS vst3
    UI_TYPE opengl
    USE_FILE_BROWSER
    FILES_DSP ${MIDICHOPPER_DSP_SOURCES}
    FILES_UI ${MIDICHOPPER_UI_SOURCES})
  target_compile_definitions(SMS-Midichopper-VST3 INTERFACE
    MIDICHOPPER_VST3_UI_BRIDGE=1)

  dpf__determine_vst3_package_architecture(midichopper_vst3_arch)
  if(APPLE)
    set(midichopper_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-Midichopper.vst3/Contents/MacOS/$<0:>")
    set(INFO_PLIST_PROJECT_NAME "SMS-Midichopper")
    configure_file("${SMS_DPF_ROOT}/utils/plugin.bundle/Contents/Info.plist"
      "${PROJECT_BINARY_DIR}/bin/SMS-Midichopper.vst3/Contents/Info.plist" @ONLY)
    file(COPY "${SMS_DPF_ROOT}/utils/plugin.bundle/Contents/PkgInfo"
      DESTINATION "${PROJECT_BINARY_DIR}/bin/SMS-Midichopper.vst3/Contents")
  elseif(WIN32)
    set(midichopper_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-Midichopper.vst3/Contents/${midichopper_vst3_arch}-win/$<0:>")
  else()
    set(midichopper_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-Midichopper.vst3/Contents/${midichopper_vst3_arch}-linux/$<0:>")
  endif()
  set_target_properties(SMS-Midichopper-VST3-vst3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${midichopper_vst3_binary_dir}"
    OUTPUT_NAME "SMS-Midichopper")
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

set(midichopper_targets SMS-Midichopper)
if(MIDICHOPPER_BUILD_VST3)
  list(APPEND midichopper_targets SMS-Midichopper-VST3)
endif()
foreach(midichopper_target IN LISTS midichopper_targets)
  target_include_directories(${midichopper_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
  target_link_libraries(${midichopper_target} PUBLIC sms_common_ui)
  target_link_libraries(${midichopper_target}-dsp PRIVATE midichopper_core)
  target_compile_definitions(${midichopper_target}-dsp PRIVATE
    MIDICHOPPER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MIDICHOPPER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MIDICHOPPER_VERSION_PATCH=${PROJECT_VERSION_PATCH})
endforeach()

if(BUILD_TESTING)
  find_package(Threads REQUIRED)

  add_executable(midichopper_core_tests tests/core_tests.cpp)
  target_link_libraries(midichopper_core_tests PRIVATE midichopper_core)
  add_test(NAME sampler-core COMMAND midichopper_core_tests)

  add_executable(midichopper_state_codec_tests
    tests/state_codec_tests.cpp
    src/plugin/StateCodec.cpp)
  target_include_directories(midichopper_state_codec_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  add_test(NAME state-codec COMMAND midichopper_state_codec_tests)

  add_executable(midichopper_ui_geometry_tests tests/ui_geometry_tests.cpp)
  target_include_directories(midichopper_ui_geometry_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
  target_link_libraries(midichopper_ui_geometry_tests PRIVATE sms_common_ui)
  add_test(NAME ui-geometry COMMAND midichopper_ui_geometry_tests)

  add_executable(midichopper_ui_message_bus_tests tests/ui_message_bus_tests.cpp)
  target_include_directories(midichopper_ui_message_bus_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(midichopper_ui_message_bus_tests PRIVATE Threads::Threads)
  add_test(NAME ui-message-bus COMMAND midichopper_ui_message_bus_tests)

  add_executable(midichopper_wav_codec_tests
    tests/wav_codec_tests.cpp
    ../Common-Src/Audio/WavCodec.cpp
    src/plugin/PadFileActions.cpp)
  target_include_directories(midichopper_wav_codec_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(midichopper_wav_codec_tests PRIVATE Threads::Threads)
  add_test(NAME wav-codec COMMAND midichopper_wav_codec_tests)
endif()

install(
  DIRECTORY "${PROJECT_BINARY_DIR}/bin/SMS-Midichopper.lv2"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2")
