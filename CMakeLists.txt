cmake_minimum_required(VERSION 3.16)

project(avc_project LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JM)
add_subdirectory(avcdec)
add_subdirectory(player_mdi)
