set(CMAKE_THREAD_PREFER_PTHREAD)
find_package(Threads)

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
	Threads::Threads
)

target_compile_definitions(gtest
  PUBLIC
    GTEST_HAS_TR1_TUPLE=0
)

add_library(gtest-main
	src/gtest_main.cc
)

target_link_libraries(gtest-main
  PUBLIC
    gtest
)
