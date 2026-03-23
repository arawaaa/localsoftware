add_library(landing)
target_sources(landing
    PUBLIC
        aio_landing_accepter.cpp aio_landing_server.cpp s40_client.cpp
)
target_link_libraries(landing Boost::boost Boost::json common)
target_include_directories(landing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../)
