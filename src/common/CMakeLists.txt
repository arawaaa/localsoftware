add_library(common)
target_sources(common
    PUBLIC
        file.cpp
        http_manager.cpp
        readwrite_http.cpp
        readwrite_bytes.cpp
        tls_socket.cpp
        websocket_socket.cpp
        event.cpp
        async_handler.cpp
        thread_data.cpp
        coro_types.cpp
)

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common Boost::boost OpenSSL::SSL TBB::tbb PkgConfig::uring alloc logging)
