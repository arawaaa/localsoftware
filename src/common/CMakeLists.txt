add_library(common)
target_sources(common
    PUBLIC
        file.cpp
        http_manager.cpp
        inet_socket_read_write_event_bytes.cpp
        inet_socket_read_write_event_http.cpp
        inet_socket_read_write_event_httpc.cpp
        inet_socket_tls_event.cpp
        inet_websocket_event.cpp
        io_event.cpp
        io_uring_manager.cpp
)

target_link_libraries(common Boost::boost)
