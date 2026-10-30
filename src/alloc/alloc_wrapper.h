#pragma once

#include <cstddef>
#include <list>
#include <array>
#include <unordered_map>
#include <sys/mman.h>

using namespace std;

class AllocatorWrapper {
    static constexpr size_t BLOCK_SIZE_MMAP = 16 * 1024;
    static constexpr size_t BLOCKS_IN_ARENA = 64;
    static constexpr size_t ARENA_SIZE = BLOCK_SIZE_MMAP * BLOCKS_IN_ARENA;

    struct SegmentInfo {
        unsigned int length : 7 = 0;
        unsigned int back : 7 = 0;
        bool occupied : 1 = false;
        bool interior : 1 = true;
    };
    list<void*> arenas_;
    unordered_map<void*, size_t> custom_arenas_;
    unordered_map<void*, pair<size_t, array<SegmentInfo, BLOCKS_IN_ARENA>>> segment_info_;

public:
    AllocatorWrapper();

    void* bmalloc(unsigned num);

    void bfree(void* p);

    void brealloc();

private:
    void* add_arena(unsigned num = BLOCKS_IN_ARENA);
};
