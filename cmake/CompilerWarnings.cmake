function(kasane_set_project_warnings target)
  if(MSVC)
    target_compile_options(${target} INTERFACE /W4 /permissive-)
    if(KASANE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} INTERFACE /WX)
    endif()
  else()
    target_compile_options(
      ${target}
      INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
    if(KASANE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} INTERFACE -Werror)
    endif()
  endif()
endfunction()
