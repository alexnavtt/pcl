cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(alignment_prerejective)

find_package(PCL 1.7 REQUIRED COMPONENTS io registration segmentation visualization)

add_executable (alignment_prerejective alignment_prerejective.cpp)
target_link_libraries (alignment_prerejective ${PCL_LIBRARIES})
