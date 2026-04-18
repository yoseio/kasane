if(NOT KASANE_BUILD_CLANG_FRONTEND)
  message(STATUS "Kasane: frontends/clang/cpp is disabled by configuration.")
  return()
endif()

kasane_find_llvm_clang()
if(NOT KASANE_LLVM_CLANG_FOUND)
  return()
endif()

add_executable(kasane-mini-extractor
  src/main.cpp
)

target_include_directories(kasane-mini-extractor
  SYSTEM PRIVATE
    ${KASANE_LLVM_INCLUDE_DIRS}
    ${KASANE_CLANG_INCLUDE_DIRS}
)

target_link_libraries(kasane-mini-extractor
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

kasane_install_target(kasane-mini-extractor)
