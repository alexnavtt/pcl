cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(range_image_border_extraction)

find_package(PCL 1.3 REQUIRED)

add_executable (range_image_border_extraction range_image_border_extraction.cpp)
target_link_libraries (range_image_border_extraction ${PCL_LIBRARIES})

