add_library(gtest
	src/gtest-all.cc
)

target_include_directories(gtest
  PUBLIC
    include
  PRIVATE
    .
)

target_link_libraries(gtest
  PRIVATE
    pthread
)

add_library(gtest-main
	src/gtest_main.cc
)

target_link_libraries(gtest-main
  PUBLIC
    gtest
)
