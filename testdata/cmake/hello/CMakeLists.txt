cmake_minimum_required(VERSION 3.25)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(KasaneHello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hello_lib
  src/hello.cpp
)

target_include_directories(hello_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(hello_app
  src/main.cpp
)

target_link_libraries(hello_app
  PRIVATE
    hello_lib
)
