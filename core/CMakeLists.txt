include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Warnings.cmake)
find_package(Threads REQUIRED)

add_library(enginesense_core
  src/signals.cpp
  src/dtc.cpp
  src/fault.cpp
  src/rules.cpp
  src/fault_engine.cpp
  src/simulator.cpp
  src/scenarios.cpp
  src/log_io.cpp
  src/monitor.cpp
  src/c_api.cpp)
target_include_directories(enginesense_core PUBLIC include)
target_compile_features(enginesense_core PUBLIC cxx_std_17)
target_link_libraries(enginesense_core PUBLIC Threads::Threads)
set_target_properties(enginesense_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
enginesense_set_warnings(enginesense_core)
enginesense_enable_tidy(enginesense_core)
