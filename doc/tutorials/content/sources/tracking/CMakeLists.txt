cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(openni_tracking)

find_package(PCL 1.7 REQUIRED)

add_executable (tracking_sample tracking_sample.cpp)
target_link_libraries (tracking_sample ${PCL_LIBRARIES})
