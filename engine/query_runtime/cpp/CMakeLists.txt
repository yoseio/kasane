add_library(kasane_query_runtime STATIC
  src/finding.cpp
)

add_library(kasane::query_runtime ALIAS kasane_query_runtime)

target_include_directories(kasane_query_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kasane_query_runtime
  PUBLIC
    kasane::project_options
)

kasane_install_target(kasane_query_runtime)
