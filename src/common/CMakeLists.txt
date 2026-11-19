add_library(common)
target_sources(common
    PUBLIC
        file.cpp
        http_manager.cpp
        readwrite_http.cpp
        socket_adaptors/readwrite_bytes.cpp
        socket_adaptors/tls_socket.cpp
        # socket_adaptors/websocket_socket.cpp
        event.cpp
        async_handler.cpp
        thread_data.cpp
        coroutine/readiness.cpp
        coroutine/pinned.cpp
)

if(LOCAL)
    target_compile_definitions(common PUBLIC ISLOCAL)
endif()

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common Boost::boost OpenSSL::SSL TBB::tbb PkgConfig::uring alloc logging)
