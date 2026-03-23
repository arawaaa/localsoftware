add_library(reverse_proxy)
target_sources(reverse_proxy
    PUBLIC
        reverse_proxy_accept.cpp reverse_proxy_manager.cpp reverse_proxy_server.cpp
)
target_link_libraries(reverse_proxy Boost::boost common)
target_include_directories(reverse_proxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../)
