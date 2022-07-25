cmake_minimum_required (VERSION 2.8.11)
project (tsjson)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -ggdb -fsanitize=address,undefined")
#set(CMAKE_C_FLAGS_DEBUG "-g -ggdb")
set(CMAKE_C_FLAGS_RELEASE "-O2")

add_library(tsjson STATIC src/tsjson.c)

target_include_directories(tsjson PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(tsjson PUBLIC)

add_subdirectory(tests)


