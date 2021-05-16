cmake_minimum_required(VERSION 3.19)
project(chat)

set(CMAKE_CXX_STANDARD 14)

add_executable(chat main.cpp ngx_queue.c)

target_link_libraries(chat uv pthread)