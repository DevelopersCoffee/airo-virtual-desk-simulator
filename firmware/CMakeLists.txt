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
  airo-core/src/api.cpp
  airo-core/src/app_server.cpp)
target_include_directories(airo_core PUBLIC airo-core/include)

# Board code: Esp32HardwareAdapter, BenchIo, BenchLink, pin map. Portable;
# the ESP32-S3 build (firmware/esp32) compiles the same files.
add_library(airo_board
  board/src/pin_map.cpp
  board/src/bench_io.cpp
  board/src/esp32_hardware_adapter.cpp
  board/src/bench_link.cpp)
target_include_directories(airo_board PUBLIC board/include)
target_link_libraries(airo_board PUBLIC airo_core)

# Laptop host: VirtualHardwareAdapter + REST/WebSocket transport.
add_library(airo_host_lib
  host/virtual_hardware_adapter.cpp
  host/net_server.cpp)
target_include_directories(airo_host_lib PUBLIC host)
target_link_libraries(airo_host_lib PUBLIC airo_core airo_board)

add_executable(airo-host host/main.cpp)
target_link_libraries(airo-host PRIVATE airo_host_lib)

enable_testing()
add_executable(airo_core_tests
  tests/test_main.cpp
  tests/test_command_service.cpp
  tests/test_json_api.cpp
  tests/test_virtual_adapter.cpp
  tests/test_board.cpp)
target_link_libraries(airo_core_tests PRIVATE airo_host_lib)
add_test(NAME airo_core_tests COMMAND airo_core_tests)
