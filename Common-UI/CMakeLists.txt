add_library(sms_common_ui INTERFACE)

add_library(sms_file_io STATIC
  ../Common-Src/IO/FileIO.cpp
  ../Common-Src/IO/DefaultsFile.cpp)
target_include_directories(sms_file_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src")
target_compile_features(sms_file_io PUBLIC cxx_std_20)
set_target_properties(sms_file_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
  target_compile_definitions(sms_file_io PRIVATE _WIN32_WINNT=0x0A00)
  target_link_libraries(sms_file_io PRIVATE shell32 ole32)
endif()
if(MSVC)
  target_compile_options(sms_common_ui INTERFACE /utf-8)
endif()
target_link_libraries(sms_common_ui INTERFACE sms_file_io)

target_include_directories(sms_common_ui INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../Common-Src")
