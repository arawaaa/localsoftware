#include <cstddef>
#include <cstdint>
#include <list>
#include <array>
#include <ranges>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>

using namespace std;

class AllocatorWrappr {
    static constexpr size_t BLOCK_SIZE = 16 * 1024;
    static constexpr size_t BLOCKS_IN_ARENA = 64;
    static constexpr size_t ARENA_SIZE = BLOCK_SIZE * BLOCKS_IN_ARENA;

    struct SegmentInfo {
        unsigned int length : 7 = 0;
        unsigned int back : 7 = 0;
        bool occupied : 1 = false;
        bool interior : 1 = true;
    };
    list<void*> arenas_;
    unordered_map<void*, size_t> custom_arenas_;
    int num_empty_arenas_;
    unordered_map<void*, pair<size_t, array<SegmentInfo, BLOCKS_IN_ARENA>>> segment_info_;

public:
    AllocatorWrappr() {
        // Initialize an arena
        void* arena = mmap(nullptr, ARENA_SIZE, PROT_WRITE | PROT_READ, MAP_ANONYMOUS, -1, 0);
        arenas_.push_back(arena);
    }

    void* bmalloc(unsigned num) {
        if (num > BLOCKS_IN_ARENA) {
            return add_arena(num);
        }

        for (void* arena : arenas_) {
            if (custom_arenas_.contains(arena) || segment_info_[arena].first < num) continue;

            auto& arr = segment_info_[arena].second;
            int found_block = -1;
            for (size_t i = 0; i < arr.size(); i += arr[i].length) {
                auto& segment = arr[i];
                if (!(segment.interior || segment.occupied) && segment.length >= num) {
                    found_block = i;

                    if (segment.length == num && i + segment.length < arr.size()) {
                        arr[i + segment.length].back = i;
                    } else if (segment.length > num) {
                        // Subdivide block, the following block (if any) will be occupied by induction
                        arr[i + num] = SegmentInfo {
                            .length = segment.length - num,
                            .back = static_cast<unsigned int>(i),
                            .occupied = false,
                            .interior = false
                        };
                        if (i + segment.length < arr.size())
                            arr[i + segment.length].back = i + num;
                    }
                    segment.length = num;
                    segment.occupied = true;
                }
            }

            if (found_block != -1) {
                return static_cast<char*>(arena) + found_block * BLOCK_SIZE;
            }
        }

        void* new_arena = add_arena();
        if (num < BLOCKS_IN_ARENA) {
            segment_info_[new_arena].second[num] = SegmentInfo {
                .length = static_cast<unsigned int>(BLOCKS_IN_ARENA - num),
                .back = 0,
                .occupied = false,
                .interior = false
            };
        }
        segment_info_[new_arena].second[0].occupied = true;
        segment_info_[new_arena].second[0].length = num;
        return new_arena;
    }

    void bfree(void* p) {
        if (custom_arenas_.contains(p)) {
            arenas_.remove(p);
            // Immediately unmap, with the assumption that allocations this large will be used for long-running purposes
            munmap(p, custom_arenas_[p] * BLOCK_SIZE);
            custom_arenas_.erase(p);
            return;
        }

        for (void* arena : arenas_ | views::reverse) {
            if (p > arena) {
                size_t position = (reinterpret_cast<uint64_t>(p) - reinterpret_cast<uint64_t>(arena)) / BLOCK_SIZE;
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

                if (!arr[0].occupied && arr[0].length == BLOCKS_IN_ARENA) {
                    num_empty_arenas_++;

                    if (num_empty_arenas_ > 2) {
                        arenas_.remove(arena);
                        segment_info_.erase(arena);
                        munmap(arena, BLOCKS_IN_ARENA * BLOCK_SIZE);
                        num_empty_arenas_--;
                    }
                }
            }
        }
    }

private:
    void* add_arena(unsigned num = BLOCKS_IN_ARENA) {
        void* arena = mmap(nullptr, num * BLOCK_SIZE, PROT_WRITE | PROT_READ, MAP_ANONYMOUS, -1, 0);
        arenas_.insert(
            find_if(arenas_.begin(), arenas_.end(), [arena](void* p) {
                return p > arena;
            }),
            arena);
        if (num > BLOCKS_IN_ARENA) {
            custom_arenas_.emplace(arena, num);
        }
        return arena;
    }
};
