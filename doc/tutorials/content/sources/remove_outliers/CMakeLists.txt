cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(remove_outliers)

find_package(PCL 1.2 REQUIRED)

add_executable (remove_outliers remove_outliers.cpp)
target_link_libraries (remove_outliers ${PCL_LIBRARIES})
