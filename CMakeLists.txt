cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z -Wall -Wextra ")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fcoroutines-ts -stdlib=libc++")

find_library(SDL2_LIB	NAMES SDL2)

include_directories(${CMAKE_SOURCE_DIR})
add_executable(cppmandel
  main.cpp
  Core/BitArray.cpp
  Core/Memory.cpp
  Core/Slice.cpp
  Core/Utils.cpp
  Math/Color.cpp
  Math/Mat.cpp
)
target_link_libraries(cppmandel ${SDL2_LIB} GL)