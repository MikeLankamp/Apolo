add_library(gmock
	src/gmock-all.cc
)

target_include_directories(gmock
  PUBLIC
    include
  PRIVATE
    .
)

target_link_libraries(gmock
  PUBLIC
    gtest
)

add_library(gmock-main
	src/gmock_main.cc
)

target_link_libraries(gmock-main
  PUBLIC
    gmock
)
