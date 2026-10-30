#include "alloc_wrapper.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <array>
#include <ranges>
#include <algorithm>
#include <unordered_map>
#include <sys/mman.h>

using namespace std;

AllocatorWrapper::AllocatorWrapper() {
    // Initialize an arena
    add_arena();
}

void* AllocatorWrapper::bmalloc(unsigned num) {
    if (num > BLOCKS_IN_ARENA) {
        return add_arena(num);
    }

    for (void* arena : arenas_) {
        if (custom_arenas_.contains(arena) /*|| segment_info_[arena].first < num*/) continue;

        auto& arr = segment_info_[arena].second;
        int found_block = -1;
        unsigned int maxsize_excluding_pick = 0;
        for (size_t i = 0; i < arr.size(); i += arr[i].length) {
            auto& segment = arr[i];
            if (!(segment.interior || segment.occupied) && segment.length >= num) {
                found_block = i;
            } else {
                maxsize_excluding_pick = max(maxsize_excluding_pick, segment.length);
            }
        }

        // segment_info_[arena].first = maxsize_excluding_pick;

        if (found_block != -1) {
            auto& segment = arr[found_block];

            if (segment.length == num && static_cast<size_t>(found_block) + segment.length < arr.size()) {
                arr[found_block + segment.length].back = found_block;
            } else if (segment.length > num) {
                // Subdivide block, the following block (if any) will be occupied by induction
                arr[found_block + num] = SegmentInfo {
                    .length = segment.length - num,
                    .back = static_cast<unsigned int>(found_block),
                    .occupied = false,
                    .interior = false
                };
                if (static_cast<size_t>(found_block) + segment.length < arr.size())
                    arr[found_block + segment.length].back = found_block + num;
            }
            segment.length = num;
            segment.occupied = true;

            return static_cast<char*>(arena) + static_cast<size_t>(found_block) * BLOCK_SIZE_MMAP;
        }
    }

    void* new_arena = add_arena();
    if (num <= BLOCKS_IN_ARENA) {
        segment_info_[new_arena].second[0].occupied = true;
        segment_info_[new_arena].second[0].length = num;
        if (num < BLOCKS_IN_ARENA) {
            segment_info_[new_arena].second[num].occupied = false;
            segment_info_[new_arena].second[num].interior = false;
            segment_info_[new_arena].second[num].back = 0;
            segment_info_[new_arena].second[num].length = BLOCKS_IN_ARENA - num;
        }
    }
    return new_arena;
}

void AllocatorWrapper::bfree(void* p) {
    if (custom_arenas_.contains(p)) {
        arenas_.remove(p);
        // Immediately unmap, with the assumption that allocations this large will be used for long-running purposes
        munmap(p, custom_arenas_[p] * BLOCK_SIZE_MMAP);
        custom_arenas_.erase(p);
        return;
    }

    for (void* arena : arenas_ | views::reverse) {
        if (p >= arena) {
            size_t position = (reinterpret_cast<uint64_t>(p) - reinterpret_cast<uint64_t>(arena)) / BLOCK_SIZE_MMAP;
            auto& arr = segment_info_[arena].second;
            auto back = arr[position].back;
            auto forward = position + arr[position].length;
            if (forward < arr.size() && !arr[forward].occupied) {
                arr[position].length += arr[forward].length;
                arr[forward].interior = true;
            }

            if (position != 0 && !arr[back].occupied) {
                arr[back].length += arr[position].length;
                arr[position].interior = true;

                auto next_in_line = back + arr[back].length;
                if (next_in_line < arr.size())
                    arr[next_in_line].back = back;
            } else {
                arr[position].occupied = false;

                auto next_in_line = position + arr[position].length;
                if (next_in_line < arr.size())
                    arr[next_in_line].back = position;
            }

            if (!arr[0].occupied && arr[0].length == BLOCKS_IN_ARENA && arenas_.size() > 3) {
                arenas_.remove(arena);
                segment_info_.erase(arena);
                munmap(arena, BLOCKS_IN_ARENA * BLOCK_SIZE_MMAP);
            }
            return;
        }
    }
}

// TODO implement
void AllocatorWrapper::brealloc() {

}

void* AllocatorWrapper::add_arena(unsigned num) {
    void* arena = mmap(nullptr, num * BLOCK_SIZE_MMAP, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (num <= BLOCKS_IN_ARENA) {
        arenas_.insert(
            find_if(arenas_.begin(), arenas_.end(), [arena](void* p) {
                return p > arena;
            }),
            arena);
        segment_info_[arena].first = num;
        segment_info_[arena].second[0] = SegmentInfo {
            .length = num,
            .back = 0,
            .occupied = false,
            .interior = false
        };
    } else {
        custom_arenas_.emplace(arena, num);
    }
    return arena;
}
