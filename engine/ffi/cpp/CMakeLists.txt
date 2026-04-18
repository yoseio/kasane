add_library(kasane_c_api STATIC
  src/kasane_c_api.cpp
)

add_library(kasane::c_api ALIAS kasane_c_api)

target_include_directories(kasane_c_api
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kasane_c_api
  PUBLIC
    kasane::project_options
)

kasane_install_target(kasane_c_api)
