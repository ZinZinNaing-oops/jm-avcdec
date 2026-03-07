cmake_minimum_required(VERSION 3.16)

project(avc_project LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable narrowing warning for JM compatibility
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-c++11-narrowing)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-narrowing)
endif()

add_subdirectory(JM)
add_subdirectory(avcdec)
add_subdirectory(player_mdi)
