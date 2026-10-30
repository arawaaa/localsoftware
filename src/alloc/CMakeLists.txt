add_library(alloc)
target_sources(alloc
    PUBLIC
        alloc_wrapper.cpp
)
target_include_directories(alloc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(alloc common)
