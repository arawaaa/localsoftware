cmake_minimum_required(VERSION 4.3)

project(localsoftware VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Boost REQUIRED system json)

add_subdirectory(src)
