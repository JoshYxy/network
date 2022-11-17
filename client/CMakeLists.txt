cmake_minimum_required(VERSION 3.17)
project(client C)

set(CMAKE_C_STANDARD 11)

include_directories(.)
link_libraries(ws2_32 wsock32)
add_executable(client
        ftpclient.c
        ftpclient.h)

# mac
#cmake_minimum_required(VERSION 3.20)
#project(client C)
#
#set(CMAKE_C_STANDARD 99)
#
#add_executable(client ftpclient.c ftpclient.h)