cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(ground_based_rgbd_people_detector)
find_package(PCL 1.7 REQUIRED)

add_executable (ground_based_rgbd_people_detector MACOSX_BUNDLE src/main_ground_based_people_detection.cpp)
target_link_libraries (ground_based_rgbd_people_detector ${PCL_LIBRARIES})
