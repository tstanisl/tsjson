set(CMAKE_C_STANDARD 99)

add_executable (test test.c)
target_link_libraries(test PUBLIC tsjson)

