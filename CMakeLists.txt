cmake_minimum_required(VERSION 3.25)

project(Kasane VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KASANE_BUILD_TESTS "Build C++ tests" ON)
option(KASANE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(KASANE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(KASANE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(KASANE_BUILD_CLANG_FRONTEND "Build the Clang-based frontend when LLVM/Clang are available" ON)

include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(LLVM)
include(Install)

add_library(kasane_project_options INTERFACE)
add_library(kasane::project_options ALIAS kasane_project_options)
target_compile_features(kasane_project_options INTERFACE cxx_std_20)

kasane_set_project_warnings(kasane_project_options)
kasane_enable_sanitizers(kasane_project_options)

add_subdirectory(engine/graph/cpp)
add_subdirectory(engine/facts/cpp)
add_subdirectory(engine/query_runtime/cpp)
add_subdirectory(engine/ffi/cpp)
add_subdirectory(frontends/clang/cpp)
add_subdirectory(frontends/fast/cpp)
