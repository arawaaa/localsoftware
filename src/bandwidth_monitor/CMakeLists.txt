add_library(bandwidth)

target_sources(bandwidth
    PUBLIC
        bandwidth_accept.cpp bandwidth_data_timer.cpp monitoring_server.cpp
)
target_link_libraries(bandwidth Boost::boost common)
