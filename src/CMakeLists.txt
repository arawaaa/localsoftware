add_subdirectory(logging)
add_subdirectory(alloc)
add_subdirectory(common)
add_subdirectory(aio_landing)
add_subdirectory(bandwidth_monitor)

add_executable(localsoftware localsoftware.cpp)

target_link_libraries(localsoftware Boost::boost common landing bandwidth logging alloc)
