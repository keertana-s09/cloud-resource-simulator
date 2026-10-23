cmake_minimum_required(VERSION 3.14)
project(CloudResourceSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Everything except main.cpp goes into a library shared by the app and the tests.
add_library(crs_core
  src/Task.cpp
  src/VM.cpp
  src/ResourceAllocator.cpp
  src/Scheduler.cpp
  src/DecisionAuditor.cpp
  src/SimulationEvent.cpp
  src/Workload.cpp
  src/Simulator.cpp
  src/Metrics.cpp
  src/Replay.cpp
)
target_include_directories(crs_core PUBLIC include)

add_executable(CloudResourceSimulator src/main.cpp)
target_link_libraries(CloudResourceSimulator PRIVATE crs_core)

# Optional localhost web view (separate program; the core app above needs no server).
add_executable(CloudResourceSimulatorWeb web/server.cpp)
target_link_libraries(CloudResourceSimulatorWeb PRIVATE crs_core)
if(WIN32)
  target_link_libraries(CloudResourceSimulatorWeb PRIVATE ws2_32)
endif()

enable_testing()
add_executable(crs_tests tests/tests.cpp)
target_link_libraries(crs_tests PRIVATE crs_core)
add_test(NAME crs_tests COMMAND crs_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
