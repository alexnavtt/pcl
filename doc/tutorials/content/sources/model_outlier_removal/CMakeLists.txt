cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(model_outlier_removal)

find_package(PCL 1.7 REQUIRED)

add_executable (model_outlier_removal model_outlier_removal.cpp)
target_link_libraries (model_outlier_removal ${PCL_LIBRARIES})
