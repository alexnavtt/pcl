cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(moment_of_inertia)

find_package(PCL 1.8 REQUIRED)

add_executable (moment_of_inertia moment_of_inertia.cpp)
target_link_libraries (moment_of_inertia ${PCL_LIBRARIES})

