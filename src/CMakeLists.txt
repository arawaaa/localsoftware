add_subdirectory(common)
add_subdirectory(aio_landing)
add_subdirectory(bandwidth_monitor)
add_subdirectory(reverse_proxy)

add_executable(wlan_monitor wlan_monitor.cpp)
target_link_libraries(wlan_monitor Boost::boost common landing bandwidth reverse_proxy)
