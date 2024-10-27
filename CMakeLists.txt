cmake_minimum_required(VERSION 3.29)
project(Practice_2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY /home/golimghost/Desktop/CProj/Practice_2/bin)
include_directories(.)

add_executable(server
    libs.h
    main.cpp
    structures.h
    structures.cpp
    )

add_executable(client client.cpp)
