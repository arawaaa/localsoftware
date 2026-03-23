add_library(bandwidth)
target_sources(bandwidth
    PUBLIC
        accept_event.cpp bandwidth_data_timer_event.cpp bandwidth_monitoring_server.cpp
)
target_link_libraries(bandwidth Boost::boost common)
target_include_directories(bandwidth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../)
