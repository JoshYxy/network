# win cmake list
#cmake_minimum_required(VERSION 3.17)
#project(server C)
#
#set(CMAKE_C_STANDARD 11)
#
#include_directories(.)
#link_libraries(ws2_32 wsock32)
#add_executable(server
#        ftpserver.c
#        ftpserver.h)

# mac cmakelists
cmake_minimum_required(VERSION 3.17)
project(server C)

set(CMAKE_C_STANDARD 99)

add_executable(server ftpserver.c ftpserver.h)
