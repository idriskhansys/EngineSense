include(FetchContent)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(enginesense_tests
  test_j1939.cpp
  test_signals.cpp
  test_dtc.cpp
  test_log_io.cpp
  test_fault_engine.cpp
  test_simulator.cpp
  test_scenarios.cpp
  test_regression.cpp
  test_monitor.cpp
  test_c_api.cpp
  test_robustness.cpp
  test_performance.cpp)
target_link_libraries(enginesense_tests PRIVATE enginesense_core GTest::gtest_main)
enginesense_set_warnings(enginesense_tests)
target_compile_definitions(enginesense_tests PRIVATE
  ES_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  # Performance budgets only mean something in an optimised build; Debug / sanitizer runs use
  # relaxed numbers so they still catch gross regressions without flaking.
  ES_MIN_FPS=$<IF:$<CONFIG:Release>,100000,5000>
  ES_SNAPSHOT_P99_MS=$<IF:$<CONFIG:Release>,5,16>)

include(GoogleTest)
gtest_discover_tests(enginesense_tests DISCOVERY_TIMEOUT 120)

# Verifies: REQ-ARCH-001
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_test(NAME core_has_no_platform_dependencies
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_core_deps.py)
endif()
