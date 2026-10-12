cmake_minimum_required(VERSION 3.22)

project(SMSAnvilleSampler VERSION 0.0.8 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANVILLESAMPLER_BUILD_VST3 "Also build a VST3 bundle" OFF)
option(ANVILLESAMPLER_BUILD_CLAP "Also build a CLAP bundle" OFF)
set(ANVILLESAMPLER_HELP_REF "main" CACHE STRING
  "Git ref used for Online Help (main for development builds, release tag for releases)")
if(NOT ANVILLESAMPLER_HELP_REF MATCHES "^(main|SMS-AnvilleSampler-v[0-9]+\\.[0-9]+\\.[0-9]+)$")
  message(FATAL_ERROR "ANVILLESAMPLER_HELP_REF must be main or an SMS-AnvilleSampler-vX.Y.Z tag")
endif()
if(NOT ANVILLESAMPLER_HELP_REF STREQUAL "main" AND
   NOT ANVILLESAMPLER_HELP_REF STREQUAL "SMS-AnvilleSampler-v${PROJECT_VERSION}")
  message(FATAL_ERROR "ANVILLESAMPLER_HELP_REF must match the CMake project version")
endif()

set(SMS_DPF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/DPF")

if(NOT EXISTS "${SMS_DPF_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "DPF is missing. Run: git submodule update --init --recursive")
endif()

if(UNIX AND NOT APPLE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ANVILLESAMPLER_DBUS REQUIRED dbus-1)
endif()

set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SMS_DPF_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/DPF"
  EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Common-UI"
  "${CMAKE_CURRENT_BINARY_DIR}/Common-UI" EXCLUDE_FROM_ALL)

add_library(anvillesampler_core STATIC
  src/core/PadClipboard.cpp
  src/core/SamplerEngine.cpp
  src/core/SampleStoragePool.cpp)

target_include_directories(anvillesampler_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src")

set_target_properties(anvillesampler_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(anvillesampler_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
  target_compile_options(anvillesampler_core PRIVATE /W4 /permissive-)
endif()

set(ANVILLESAMPLER_PLUGIN_FORMATS lv2)
if(ANVILLESAMPLER_BUILD_CLAP)
  list(APPEND ANVILLESAMPLER_PLUGIN_FORMATS clap)
endif()

set(ANVILLESAMPLER_DSP_SOURCES
  ../Common-Src/Audio/WavCodec.cpp
  src/plugin/MidichopperPlugin.cpp
  src/plugin/PadFileActions.cpp
  src/plugin/PadWorkflows.cpp
  src/plugin/StateCodec.cpp)
set(ANVILLESAMPLER_ARTWORK_DIR "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ANVILLESAMPLER_ARTWORK_DIR}")
add_custom_command(
  OUTPUT
    "${ANVILLESAMPLER_ARTWORK_DIR}/anvillesampler_artwork.cpp"
    "${ANVILLESAMPLER_ARTWORK_DIR}/anvillesampler_artwork.hpp"
  COMMAND "${Python3_EXECUTABLE}" "${SMS_DPF_ROOT}/utils/res2c.py"
    anvillesampler_artwork "${CMAKE_CURRENT_SOURCE_DIR}/res"
    "${ANVILLESAMPLER_ARTWORK_DIR}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/res/anville.png"
    "${SMS_DPF_ROOT}/utils/res2c.py"
  VERBATIM)
set(ANVILLESAMPLER_UI_SOURCES
  src/ui/MidichopperUI.cpp
  src/ui/MidichopperView.cpp
  "${ANVILLESAMPLER_ARTWORK_DIR}/anvillesampler_artwork.cpp"
  "${ANVILLESAMPLER_ARTWORK_DIR}/anvillesampler_artwork.hpp")

dpf_add_plugin(SMS-AnvilleSampler
  TARGETS ${ANVILLESAMPLER_PLUGIN_FORMATS}
  UI_TYPE opengl
  USE_FILE_BROWSER
  FILES_DSP ${ANVILLESAMPLER_DSP_SOURCES}
  FILES_UI ${ANVILLESAMPLER_UI_SOURCES})

# Only the VST3 variant needs same-process UI access. Keep LV2's separate UI
# binary and its existing host feature requirements unchanged.
if(ANVILLESAMPLER_BUILD_VST3)
  dpf_add_plugin(SMS-AnvilleSampler-VST3
    TARGETS vst3
    UI_TYPE opengl
    USE_FILE_BROWSER
    FILES_DSP ${ANVILLESAMPLER_DSP_SOURCES}
    FILES_UI ${ANVILLESAMPLER_UI_SOURCES})
  target_compile_definitions(SMS-AnvilleSampler-VST3 INTERFACE
    ANVILLESAMPLER_VST3_UI_BRIDGE=1)

  dpf__determine_vst3_package_architecture(anvillesampler_vst3_arch)
  if(APPLE)
    set(anvillesampler_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.vst3/Contents/MacOS/$<0:>")
    set(INFO_PLIST_PROJECT_NAME "SMS-Anville Sampler")
    configure_file("${SMS_DPF_ROOT}/utils/plugin.bundle/Contents/Info.plist"
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.vst3/Contents/Info.plist" @ONLY)
    file(COPY "${SMS_DPF_ROOT}/utils/plugin.bundle/Contents/PkgInfo"
      DESTINATION "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.vst3/Contents")
  elseif(WIN32)
    set(anvillesampler_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.vst3/Contents/${anvillesampler_vst3_arch}-win/$<0:>")
  else()
    set(anvillesampler_vst3_binary_dir
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.vst3/Contents/${anvillesampler_vst3_arch}-linux/$<0:>")
  endif()
  set_target_properties(SMS-AnvilleSampler-VST3-vst3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${anvillesampler_vst3_binary_dir}"
    OUTPUT_NAME "SMS-AnvilleSampler")
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

set(anvillesampler_targets SMS-AnvilleSampler)
if(ANVILLESAMPLER_BUILD_VST3)
  list(APPEND anvillesampler_targets SMS-AnvilleSampler-VST3)
endif()
foreach(anvillesampler_target IN LISTS anvillesampler_targets)
  target_include_directories(${anvillesampler_target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
  target_link_libraries(${anvillesampler_target} PUBLIC sms_common_ui)
  target_link_libraries(${anvillesampler_target}-dsp PRIVATE anvillesampler_core Threads::Threads)
  target_include_directories(${anvillesampler_target}-ui PRIVATE
    "${ANVILLESAMPLER_ARTWORK_DIR}")
  target_compile_definitions(${anvillesampler_target}-dsp PRIVATE
    ANVILLESAMPLER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ANVILLESAMPLER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ANVILLESAMPLER_VERSION_PATCH=${PROJECT_VERSION_PATCH})
  target_compile_definitions(${anvillesampler_target}-ui PRIVATE
    ANVILLESAMPLER_VERSION_STRING="${PROJECT_VERSION}"
    ANVILLESAMPLER_HELP_REF="${ANVILLESAMPLER_HELP_REF}")
endforeach()

# Ship an inactive, editable example. Never overwrite the user's active skin.
set(anvillesampler_skin_template "${CMAKE_CURRENT_SOURCE_DIR}/skins/placeholder")
add_custom_target(anvillesampler_skin_templates ALL
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${anvillesampler_skin_template}"
    "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.lv2/skin-template"
  VERBATIM)
if(ANVILLESAMPLER_BUILD_VST3)
  add_custom_command(TARGET anvillesampler_skin_templates POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${anvillesampler_skin_template}"
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.vst3/skin-template"
    VERBATIM)
endif()
if(ANVILLESAMPLER_BUILD_CLAP)
  add_custom_command(TARGET anvillesampler_skin_templates POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${anvillesampler_skin_template}"
      "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.skin-template"
    VERBATIM)
endif()

if(BUILD_TESTING)
  add_executable(anvillesampler_skin_catalog_tests tests/skin_catalog_tests.cpp)
  target_link_libraries(anvillesampler_skin_catalog_tests PRIVATE sms_common_ui Threads::Threads)
  add_test(NAME ui-skin-catalog COMMAND anvillesampler_skin_catalog_tests)

  add_executable(anvillesampler_skin_tests tests/skin_tests.cpp)
  target_link_libraries(anvillesampler_skin_tests PRIVATE sms_common_ui)
  add_test(NAME ui-skin COMMAND anvillesampler_skin_tests)

  add_executable(anvillesampler_skin_theme_tests tests/skin_theme_tests.cpp)
  target_include_directories(anvillesampler_skin_theme_tests PRIVATE
    "${SMS_DPF_ROOT}/distrho" "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_compile_definitions(anvillesampler_skin_theme_tests PRIVATE
    HAVE_DGL DGL_USE_FILE_BROWSER
    ANVILLESAMPLER_SKIN_TEMPLATE_DIR="${anvillesampler_skin_template}")
  target_link_libraries(anvillesampler_skin_theme_tests PRIVATE sms_common_ui dgl-opengl Threads::Threads)
  add_test(NAME ui-skin-theme COMMAND anvillesampler_skin_theme_tests)

  add_executable(anvillesampler_preview_mailbox_tests tests/preview_mailbox_tests.cpp)
  target_include_directories(anvillesampler_preview_mailbox_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(anvillesampler_preview_mailbox_tests PRIVATE Threads::Threads)
  add_test(NAME preview-mailbox COMMAND anvillesampler_preview_mailbox_tests)

  add_executable(anvillesampler_pad_workflow_tests
    tests/pad_workflow_tests.cpp
    src/plugin/PadWorkflows.cpp
    src/plugin/PadFileActions.cpp
    src/plugin/StateCodec.cpp
    ../Common-Src/Audio/WavCodec.cpp)
  target_include_directories(anvillesampler_pad_workflow_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(anvillesampler_pad_workflow_tests PRIVATE anvillesampler_core Threads::Threads)
  add_test(NAME pad-workflows COMMAND anvillesampler_pad_workflow_tests)

  add_executable(anvillesampler_core_tests tests/core_tests.cpp)
  target_link_libraries(anvillesampler_core_tests PRIVATE anvillesampler_core)
  add_test(NAME sampler-core COMMAND anvillesampler_core_tests)

  add_executable(anvillesampler_buffer_timing_tests tests/buffer_timing_tests.cpp)
  target_link_libraries(anvillesampler_buffer_timing_tests PRIVATE anvillesampler_core)
  add_test(NAME buffer-timing COMMAND anvillesampler_buffer_timing_tests)

  add_executable(anvillesampler_adsr_envelope_tests tests/adsr_envelope_tests.cpp)
  target_include_directories(anvillesampler_adsr_envelope_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src")
  add_test(NAME adsr-envelope COMMAND anvillesampler_adsr_envelope_tests)

  add_executable(anvillesampler_transfer_tests
    tests/transfer_tests.cpp
    ../Common-Src/Audio/WavCodec.cpp)
  target_link_libraries(anvillesampler_transfer_tests PRIVATE
    anvillesampler_core Threads::Threads)
  add_test(NAME sampler-transfer COMMAND anvillesampler_transfer_tests)

  add_executable(anvillesampler_state_codec_tests
    tests/state_codec_tests.cpp
    src/plugin/StateCodec.cpp)
  target_include_directories(anvillesampler_state_codec_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  add_test(NAME state-codec COMMAND anvillesampler_state_codec_tests)

  add_executable(anvillesampler_ui_geometry_tests tests/ui_geometry_tests.cpp)
  target_include_directories(anvillesampler_ui_geometry_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
  target_link_libraries(anvillesampler_ui_geometry_tests PRIVATE sms_common_ui)
  add_test(NAME ui-geometry COMMAND anvillesampler_ui_geometry_tests)

  add_executable(anvillesampler_ui_message_bus_tests tests/ui_message_bus_tests.cpp)
  target_include_directories(anvillesampler_ui_message_bus_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(anvillesampler_ui_message_bus_tests PRIVATE Threads::Threads)
  add_test(NAME ui-message-bus COMMAND anvillesampler_ui_message_bus_tests)

  add_executable(anvillesampler_wav_codec_tests
    tests/wav_codec_tests.cpp
    ../Common-Src/Audio/WavCodec.cpp
    src/plugin/PadFileActions.cpp)
  target_include_directories(anvillesampler_wav_codec_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin")
  target_link_libraries(anvillesampler_wav_codec_tests PRIVATE Threads::Threads)
  add_test(NAME wav-codec COMMAND anvillesampler_wav_codec_tests)
endif()

install(
  DIRECTORY "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.lv2"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2")
