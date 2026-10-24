cmake_minimum_required(VERSION 3.22)

project(SMSAnvilleSampler VERSION 0.0.13 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()

option(ANVILLESAMPLER_BUILD_LV2 "Build an LV2 bundle" ON)
option(ANVILLESAMPLER_BUILD_VST3 "Build a VST3 bundle" OFF)
option(ANVILLESAMPLER_BUILD_CLAP "Also build a CLAP bundle" OFF)
if(NOT ANVILLESAMPLER_BUILD_LV2 AND NOT ANVILLESAMPLER_BUILD_VST3 AND
   NOT ANVILLESAMPLER_BUILD_CLAP)
  message(FATAL_ERROR "Enable at least one Anville Sampler plugin format")
endif()
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

set(ANVILLESAMPLER_PLUGIN_FORMATS)
if(ANVILLESAMPLER_BUILD_LV2)
  list(APPEND ANVILLESAMPLER_PLUGIN_FORMATS lv2)
endif()
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

if(ANVILLESAMPLER_PLUGIN_FORMATS)
  dpf_add_plugin(SMS-AnvilleSampler
    TARGETS ${ANVILLESAMPLER_PLUGIN_FORMATS}
    UI_TYPE opengl
    USE_FILE_BROWSER
    FILES_DSP ${ANVILLESAMPLER_DSP_SOURCES}
    FILES_UI ${ANVILLESAMPLER_UI_SOURCES})
endif()

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
    RUNTIME_OUTPUT_DIRECTORY "${anvillesampler_vst3_binary_dir}"
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
get_target_property(dgl_sources dgl-opengl SOURCES)
list(FILTER dgl_sources EXCLUDE REGEX "(^|/)WebViewWin32\\.cpp$")
set_property(TARGET dgl-opengl PROPERTY SOURCES "${dgl_sources}")

set(anvillesampler_targets)
if(ANVILLESAMPLER_PLUGIN_FORMATS)
  list(APPEND anvillesampler_targets SMS-AnvilleSampler)
endif()
if(ANVILLESAMPLER_BUILD_VST3)
  list(APPEND anvillesampler_targets SMS-AnvilleSampler-VST3)
endif()
foreach(anvillesampler_target IN LISTS anvillesampler_targets)
  if(WIN32)
    get_target_property(ui_sources ${anvillesampler_target}-ui SOURCES)
    list(FILTER ui_sources EXCLUDE REGEX "(^|/)DistrhoUI_win32\\.cpp$")
    set_property(TARGET ${anvillesampler_target}-ui PROPERTY SOURCES "${ui_sources}")
  endif()
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

# Ship selectable skins and an inactive authoring example.
# Refresh on every build so manifest/artwork edits do not require relinking.
set(anvillesampler_skin_source "${CMAKE_CURRENT_SOURCE_DIR}/skins")
set(anvillesampler_bundled_skin_ids "Finnish Dream" "Looks like Wu" "White Cadillac Vinyl")
set(anvillesampler_skin_template "${anvillesampler_skin_source}/placeholder")
set(anvillesampler_packaged_skin_paths)
if(ANVILLESAMPLER_BUILD_LV2)
  list(APPEND anvillesampler_packaged_skin_paths
    "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.lv2/skin")
endif()
if(ANVILLESAMPLER_BUILD_VST3)
  list(APPEND anvillesampler_packaged_skin_paths
    "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.vst3/skin")
endif()
if(ANVILLESAMPLER_BUILD_CLAP)
  list(APPEND anvillesampler_packaged_skin_paths
    "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.skin")
endif()
add_custom_target(anvillesampler_skins ALL)
foreach(skin_path IN LISTS anvillesampler_packaged_skin_paths)
  # Remove retired bundled/forced skins from reused build output.
  # User overrides belong in installed bundles, away from generated files.
  add_custom_command(TARGET anvillesampler_skins POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${skin_path}"
      "${skin_path}s/AnvilleGraffiti" "${skin_path}s/Anville Graffiti"
      "${skin_path}s/FinnishDream"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${anvillesampler_skin_template}"
      "${skin_path}-template"
    VERBATIM)
  foreach(skin_id IN LISTS anvillesampler_bundled_skin_ids)
    add_custom_command(TARGET anvillesampler_skins POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E rm -rf "${skin_path}s/${skin_id}"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory "${anvillesampler_skin_source}/${skin_id}"
        "${skin_path}s/${skin_id}"
      VERBATIM)
  endforeach()
endforeach()
if(ANVILLESAMPLER_BUILD_LV2)
  add_dependencies(SMS-AnvilleSampler-lv2 anvillesampler_skins)
endif()
if(ANVILLESAMPLER_BUILD_VST3)
  add_dependencies(SMS-AnvilleSampler-VST3-vst3 anvillesampler_skins)
endif()
if(ANVILLESAMPLER_BUILD_CLAP)
  add_dependencies(SMS-AnvilleSampler-clap anvillesampler_skins)
endif()

if(BUILD_TESTING)
  add_executable(anvillesampler_bundled_skin_tests tests/bundled_skin_tests.cpp)
  target_link_libraries(anvillesampler_bundled_skin_tests PRIVATE sms_common_ui)
  add_dependencies(anvillesampler_bundled_skin_tests anvillesampler_skins)
  add_test(NAME ui-bundled-skins COMMAND anvillesampler_bundled_skin_tests
    "${anvillesampler_skin_source}" ${anvillesampler_packaged_skin_paths})

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
  if(MSVC)
    # DGL's private system dependency does not expose its Khronos headers to consumers.
    target_link_libraries(anvillesampler_skin_theme_tests PRIVATE dgl-system-libs-definitions)
  endif()
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

if(ANVILLESAMPLER_BUILD_LV2)
  install(
    DIRECTORY "${PROJECT_BINARY_DIR}/bin/SMS-AnvilleSampler.lv2"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2")
endif()
