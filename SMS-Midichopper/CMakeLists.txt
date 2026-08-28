cmake_minimum_required(VERSION 3.22)

project(SMSMidichopper VERSION 0.0.2 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MIDICHOPPER_BUILD_VST3 "Also build a VST3 bundle" OFF)
option(MIDICHOPPER_BUILD_CLAP "Also build a CLAP bundle" OFF)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/DPF/CMakeLists.txt")
  message(FATAL_ERROR
    "DPF is missing. Run: git submodule update --init --recursive")
endif()

set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/DPF EXCLUDE_FROM_ALL)

add_library(midichopper_core STATIC
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
if(MIDICHOPPER_BUILD_VST3)
  list(APPEND MIDICHOPPER_PLUGIN_FORMATS vst3)
endif()
if(MIDICHOPPER_BUILD_CLAP)
  list(APPEND MIDICHOPPER_PLUGIN_FORMATS clap)
endif()

dpf_add_plugin(SMS-Midichopper
  TARGETS ${MIDICHOPPER_PLUGIN_FORMATS}
  UI_TYPE opengl
  FILES_DSP
    src/plugin/MidichopperPlugin.cpp
    src/plugin/StateCodec.cpp
  FILES_UI
    src/ui/MidichopperUI.cpp)

# DPF currently passes disabled optional DGL features through generator
# expressions.  CMake 4 evaluates those expressions as true while configuring
# the DGL target, so explicitly remove features this UI does not use.
foreach(property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
  get_target_property(dgl_definitions dgl-opengl "${property}")
  if(dgl_definitions)
    list(REMOVE_ITEM dgl_definitions DGL_USE_FILE_BROWSER DGL_USE_WEB_VIEW)
    set_target_properties(dgl-opengl PROPERTIES "${property}" "${dgl_definitions}")
  endif()
endforeach()

target_include_directories(SMS-Midichopper PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")

target_link_libraries(SMS-Midichopper-dsp PRIVATE midichopper_core)
target_compile_definitions(SMS-Midichopper-dsp PRIVATE
  MIDICHOPPER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  MIDICHOPPER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  MIDICHOPPER_VERSION_PATCH=${PROJECT_VERSION_PATCH})

if(BUILD_TESTING)
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
endif()

install(
  DIRECTORY "${PROJECT_BINARY_DIR}/bin/SMS-Midichopper.lv2"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2")
