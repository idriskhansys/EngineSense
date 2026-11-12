# Build for the iOS simulator (no signing needed):
#   cmake -S platform/ios -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
#         -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
#   cmake --build build-ios --config Debug
cmake_minimum_required(VERSION 3.20)
project(EngineSenseIOS LANGUAGES C CXX OBJC OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../core core_build)

# Reusable Objective-C++ wrapper (Foundation only).
add_library(ESMonitor STATIC EngineSense/ESMonitor.mm EngineSense/ESMonitor.h)
target_include_directories(ESMonitor PUBLIC EngineSense)
target_compile_options(ESMonitor PRIVATE -fobjc-arc)
target_link_libraries(ESMonitor PUBLIC enginesense_core "-framework Foundation")

add_executable(EngineSenseApp MACOSX_BUNDLE App/main.m App/ESViewController.mm App/ESViewController.h)
target_compile_options(EngineSenseApp PRIVATE -fobjc-arc)
target_link_libraries(EngineSenseApp PRIVATE ESMonitor "-framework UIKit")
set_target_properties(EngineSenseApp PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/App/Info.plist
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.enginesense.ios)
