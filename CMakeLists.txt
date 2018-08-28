cmake_minimum_required(VERSION 3.9)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_STANDARD 17)
project(apolo LANGUAGES C CXX)


if (ENABLE_COVERAGE)
  include(CodeCoverage)

  # Set the correct compiler flags for coverage
  append_coverage_compiler_flags()

  # Do not list coverage for external libraries and test code
  list(APPEND COVERAGE_GCOVR_EXCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  )

  # Create a coverage target that generates the coverage report XML.
  setup_target_for_coverage_gcovr_xml(
    NAME ${PROJECT_NAME}-coverage
    DEPENDENCIES ${PROJECT_NAME}-test
  )
endif()


add_library(${PROJECT_NAME}
  src/apolo.cpp
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    include
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    lua
)

# Tests
include(GoogleTest)

enable_testing()
add_executable(${PROJECT_NAME}-test
  tests/arguments.cpp
  tests/register_global_function.cpp
  tests/register_simple_object.cpp
  tests/value.cpp
)
target_link_libraries(${PROJECT_NAME}-test
  PRIVATE
    apolo
    gmock
    gmock-main
)
gtest_add_tests(TARGET ${PROJECT_NAME}-test)

# External libraries
add_subdirectory(lib)
