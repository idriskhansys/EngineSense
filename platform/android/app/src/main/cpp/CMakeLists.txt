cmake_minimum_required(VERSION 3.20)
project(enginesense_android LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The portable core, built unchanged for the NDK.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../core core_build)

add_library(enginesense_jni SHARED enginesense_jni.cpp)
target_link_libraries(enginesense_jni PRIVATE enginesense_core log)
