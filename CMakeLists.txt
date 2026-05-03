cmake_minimum_required(VERSION 3.16)

project(taxi_system_refactor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

add_library(
  taxi_core
  src/taxi_domain.cpp
  src/dispatch_replay.cpp
  src/dispatch_replay_io.cpp
  src/kd_tree_spatial_index.cpp
  src/dispatch_strategy.cpp
  src/mcmf_batch_strategy.cpp
  src/requestcontext.cpp
  src/taxi_system.cpp)

target_include_directories(taxi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(taxi_core PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(taxi_demo src/main.cpp)
target_link_libraries(taxi_demo PRIVATE taxi_core)

add_executable(replay_csv_demo src/replay_csv_demo.cpp)
target_link_libraries(replay_csv_demo PRIVATE taxi_core)

add_executable(k_sweep src/k_sweep.cpp)
target_link_libraries(k_sweep PRIVATE taxi_core)

if(BUILD_TESTING)
  add_executable(kd_tree_spatial_index_test tests/kd_tree_spatial_index_test.cpp)
  target_link_libraries(kd_tree_spatial_index_test PRIVATE taxi_core)

  add_executable(requestcontext_test tests/requestcontext_test.cpp)
  target_link_libraries(requestcontext_test PRIVATE taxi_core)

  add_executable(dispatch_strategy_test tests/dispatch_strategy_test.cpp)
  target_link_libraries(dispatch_strategy_test PRIVATE taxi_core)

  add_executable(dispatch_batch_test tests/dispatch_batch_test.cpp)
  target_link_libraries(dispatch_batch_test PRIVATE taxi_core)

  add_executable(mcmf_batch_strategy_test tests/mcmf_batch_strategy_test.cpp)
  target_link_libraries(mcmf_batch_strategy_test PRIVATE taxi_core)

  add_executable(dispatch_replay_test tests/dispatch_replay_test.cpp)
  target_link_libraries(dispatch_replay_test PRIVATE taxi_core)

  add_executable(dispatch_replay_io_test tests/dispatch_replay_io_test.cpp)
  target_link_libraries(dispatch_replay_io_test PRIVATE taxi_core)

  add_executable(taxi_system_test tests/taxi_system_test.cpp)
  target_link_libraries(taxi_system_test PRIVATE taxi_core)

  add_test(NAME kd_tree_spatial_index_test COMMAND kd_tree_spatial_index_test)
  add_test(NAME requestcontext_test COMMAND requestcontext_test)
  add_test(NAME dispatch_strategy_test COMMAND dispatch_strategy_test)
  add_test(NAME dispatch_batch_test COMMAND dispatch_batch_test)
  add_test(NAME mcmf_batch_strategy_test COMMAND mcmf_batch_strategy_test)
  add_test(NAME dispatch_replay_test COMMAND dispatch_replay_test)
  add_test(NAME dispatch_replay_io_test COMMAND dispatch_replay_io_test)
  add_test(NAME taxi_system_test COMMAND taxi_system_test)
  add_test(NAME taxi_demo_smoke COMMAND taxi_demo)

  set_tests_properties(kd_tree_spatial_index_test PROPERTIES LABELS
                                                             "smoke;kd-tree")
  set_tests_properties(requestcontext_test PROPERTIES LABELS
                                                      "smoke;request-lifecycle")
  set_tests_properties(dispatch_strategy_test PROPERTIES LABELS
                                                        "smoke;dispatch")
  set_tests_properties(dispatch_batch_test PROPERTIES LABELS "smoke;batch")
  set_tests_properties(mcmf_batch_strategy_test PROPERTIES LABELS
                                                       "smoke;batch;mcmf")
  set_tests_properties(dispatch_replay_test PROPERTIES LABELS
                                                       "smoke;replay")
  set_tests_properties(dispatch_replay_io_test PROPERTIES LABELS
                                                       "smoke;replay;csv")
  set_tests_properties(taxi_system_test PROPERTIES LABELS "smoke;taxi-system")
  set_tests_properties(taxi_demo_smoke PROPERTIES LABELS "smoke;demo")

  add_custom_target(
    smoke
    COMMAND $<TARGET_FILE:kd_tree_spatial_index_test>
    COMMAND $<TARGET_FILE:requestcontext_test>
    COMMAND $<TARGET_FILE:dispatch_strategy_test>
    COMMAND $<TARGET_FILE:dispatch_batch_test>
    COMMAND $<TARGET_FILE:mcmf_batch_strategy_test>
    COMMAND $<TARGET_FILE:dispatch_replay_test>
    COMMAND $<TARGET_FILE:dispatch_replay_io_test>
    COMMAND $<TARGET_FILE:taxi_system_test>
    COMMAND $<TARGET_FILE:taxi_demo>
    DEPENDS kd_tree_spatial_index_test requestcontext_test dispatch_strategy_test
            dispatch_batch_test mcmf_batch_strategy_test dispatch_replay_test
            dispatch_replay_io_test taxi_system_test taxi_demo
    USES_TERMINAL)
endif()
