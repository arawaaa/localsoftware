add_library(logging)
target_sources(logging
    PUBLIC
        visit_stats.cpp
)
target_include_directories(logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

