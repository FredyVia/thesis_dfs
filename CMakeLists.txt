cmake_minimum_required(VERSION 3.14...3.22)

set(CMAKE_CXX_STANDARD 20)

option(BUILD_TESTS "Build the test target" OFF)
option(BUILD_BENCHMARK "Build the benchmark target" OFF)

if(BUILD_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif(BUILD_TESTS)
if(BUILD_CLIENT)
  list(APPEND VCPKG_MANIFEST_FEATURES "client")
endif(BUILD_CLIENT)
if(BUILD_BENCHMARK)
  list(APPEND VCPKG_MANIFEST_FEATURES "benchmark")
endif(BUILD_BENCHMARK)

project(Spkdfs LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(gflags CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(unofficial-brpc CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(cryptopp CONFIG REQUIRED)
pkg_check_modules(LIBERASURECODE REQUIRED IMPORTED_TARGET GLOBAL erasurecode-1)

add_subdirectory(src)


if(BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  add_subdirectory(test/src)
endif(BUILD_TESTS)

if(BUILD_BENCHMARK)
  find_package(benchmark CONFIG REQUIRED)
  add_subdirectory(benchmark)
endif(BUILD_BENCHMARK)
