cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(stick_segmentation)

find_package(PCL 1.2 REQUIRED)

add_executable (stick_segmentation stick_segmentation.cpp)
target_link_libraries (stick_segmentation ${PCL_LIBRARIES})
