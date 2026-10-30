add_library(landing)
target_sources(landing
    PUBLIC
        landing_accept.cpp landing_server.cpp s40_client.cpp
)
target_link_libraries(landing Boost::boost Boost::json common)
target_include_directories(landing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../)
