cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(range_image_visualization)

find_package(PCL 1.3 REQUIRED)

add_executable (range_image_visualization range_image_visualization.cpp)
target_link_libraries (range_image_visualization ${PCL_LIBRARIES})

