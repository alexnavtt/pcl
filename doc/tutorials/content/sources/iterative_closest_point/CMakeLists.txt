cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(iterative_closest_point)

find_package(PCL 1.2 REQUIRED)

add_executable (iterative_closest_point iterative_closest_point.cpp)
target_link_libraries (iterative_closest_point ${PCL_LIBRARIES})


