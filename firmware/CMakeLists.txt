cmake_minimum_required(VERSION 3.16)
project(airo_firmware CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# Portable firmware core: built unchanged for the laptop and (phase 2) the ESP32-S3.
add_library(airo_core
  airo-core/src/command_service.cpp
  airo-core/src/strings.cpp
  airo-core/src/json.cpp
  airo-core/src/frames.cpp
  airo-core/src/api.cpp)
target_include_directories(airo_core PUBLIC airo-core/include)

# Laptop host: VirtualHardwareAdapter + REST/WebSocket transport.
add_library(airo_host_lib
  host/virtual_hardware_adapter.cpp
  host/net_server.cpp)
target_include_directories(airo_host_lib PUBLIC host)
target_link_libraries(airo_host_lib PUBLIC airo_core)

add_executable(airo-host host/main.cpp)
target_link_libraries(airo-host PRIVATE airo_host_lib)

enable_testing()
add_executable(airo_core_tests
  tests/test_main.cpp
  tests/test_command_service.cpp
  tests/test_json_api.cpp
  tests/test_virtual_adapter.cpp)
target_link_libraries(airo_core_tests PRIVATE airo_host_lib)
add_test(NAME airo_core_tests COMMAND airo_core_tests)
