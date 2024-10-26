cmake_minimum_required(VERSION 3.29)
project(Practice_2)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(server
    libs.h
    main.cpp
    structures.cpp
    structures.h)

add_executable(client client.cpp)
