cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(cloud_viewer)

find_package(PCL 1.2 REQUIRED)

add_executable (cloud_viewer cloud_viewer.cpp)
target_link_libraries (cloud_viewer ${PCL_LIBRARIES})
