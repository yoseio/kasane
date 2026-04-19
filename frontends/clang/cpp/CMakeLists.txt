if(NOT KASANE_BUILD_CLANG_FRONTEND)
  message(STATUS "Kasane: frontends/clang/cpp is disabled by configuration.")
  return()
endif()

kasane_find_llvm_clang()
if(NOT KASANE_LLVM_CLANG_FOUND)
  return()
endif()

add_executable(kasane-clang-extractor
  src/main.cpp
  src/fact_extract_action.cpp
)

target_include_directories(kasane-clang-extractor
  SYSTEM PRIVATE
    ${KASANE_LLVM_INCLUDE_DIRS}
    ${KASANE_CLANG_INCLUDE_DIRS}
)

target_include_directories(kasane-clang-extractor
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kasane-clang-extractor
  PRIVATE
    kasane::project_options
    kasane::facts
    clangTooling
    clangFrontend
    clangAST
    clangBasic
    clangLex
    clangParse
    clangSema
    clangSerialization
    clangAnalysis
    clangEdit
    clangDriver
    clangASTMatchers
    LLVMSupport
)

kasane_install_target(kasane-clang-extractor)
