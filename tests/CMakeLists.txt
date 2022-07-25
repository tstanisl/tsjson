set(CMAKE_C_STANDARD 11)

add_executable (test test.c)
target_link_libraries(test PUBLIC tsjson)

