cmake_minimum_required(VERSION 3.16)

project(taxi_system_refactor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
  taxi_core
  src/taxi_domain.cpp
  src/kd_tree_spatial_index.cpp
  src/dispatch_strategy.cpp
  src/taxi_system.cpp)

target_include_directories(taxi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(taxi_demo src/main.cpp)
target_link_libraries(taxi_demo PRIVATE taxi_core)

add_executable(kd_tree_spatial_index_test tests/kd_tree_spatial_index_test.cpp)
target_link_libraries(kd_tree_spatial_index_test PRIVATE taxi_core)

add_executable(taxi_system_test tests/taxi_system_test.cpp)
target_link_libraries(taxi_system_test PRIVATE taxi_core)

enable_testing()
add_test(NAME kd_tree_spatial_index_test COMMAND kd_tree_spatial_index_test)
add_test(NAME taxi_system_test COMMAND taxi_system_test)
