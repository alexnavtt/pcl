cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(normal_distributions_transform)

find_package(PCL 1.5 REQUIRED)


add_executable(normal_distributions_transform normal_distributions_transform.cpp)
target_link_libraries (normal_distributions_transform ${PCL_LIBRARIES})
