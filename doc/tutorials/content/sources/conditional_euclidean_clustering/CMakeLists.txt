cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(conditional_euclidean_clustering)

find_package(PCL 1.7 REQUIRED)

add_executable (conditional_euclidean_clustering conditional_euclidean_clustering.cpp)
target_link_libraries (conditional_euclidean_clustering ${PCL_LIBRARIES})
