add_library(kasane_graph STATIC
  src/graph.cpp
)

add_library(kasane::graph ALIAS kasane_graph)

target_include_directories(kasane_graph
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kasane_graph
  PUBLIC
    kasane::project_options
)

kasane_install_target(kasane_graph)
