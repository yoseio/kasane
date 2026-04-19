add_library(kasane_facts STATIC
  src/fact_emitter.cpp
)

add_library(kasane::facts ALIAS kasane_facts)

target_include_directories(kasane_facts
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kasane_facts
  PUBLIC
    kasane::project_options
)

kasane_install_target(kasane_facts)
