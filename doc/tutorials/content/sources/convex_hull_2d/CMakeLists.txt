cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(convex_hull_2d)

find_package(PCL 1.2 REQUIRED)

add_executable (convex_hull_2d convex_hull_2d.cpp)
target_link_libraries (convex_hull_2d ${PCL_LIBRARIES})
