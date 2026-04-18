add_library(kasane_fast_frontend INTERFACE)
add_library(kasane::fast_frontend ALIAS kasane_fast_frontend)

target_link_libraries(kasane_fast_frontend
  INTERFACE
    kasane::project_options
)
