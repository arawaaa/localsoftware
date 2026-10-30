cmake_minimum_required(VERSION 4.3)

project(localsoftware VERSION 1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)

if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

add_compile_options(-Wno-interference-size)

find_package(Boost REQUIRED system json)

find_package(OpenSSL 3.5.9 REQUIRED COMPONENTS Crypto SSL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(uring REQUIRED IMPORTED_TARGET liburing)

# OneAPI Threading Building Blocks
find_package(TBB REQUIRED)

add_subdirectory(src)
