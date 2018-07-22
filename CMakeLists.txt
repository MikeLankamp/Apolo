cmake_minimum_required(VERSION 3.9)

set(CMAKE_CXX_STANDARD 17)
project(apolo LANGUAGES C CXX)

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
