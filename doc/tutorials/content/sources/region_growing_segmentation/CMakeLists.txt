cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(region_growing_segmentation)

find_package(PCL 1.5 REQUIRED)

add_executable (region_growing_segmentation region_growing_segmentation.cpp)
target_link_libraries (region_growing_segmentation ${PCL_LIBRARIES})

