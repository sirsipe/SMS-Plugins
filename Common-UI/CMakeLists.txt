add_library(sms_common_ui INTERFACE)

target_include_directories(sms_common_ui INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src")
