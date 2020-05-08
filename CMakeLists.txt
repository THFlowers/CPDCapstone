cmake_minimum_required(VERSION 3.13)
project(CPDCapstone)

set(CMAKE_C_STANDARD 99)

add_executable(parallel parallel.c)
add_executable(fib fib.c)
