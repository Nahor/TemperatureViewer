#ifndef ENGINE_FRAME_STAT_H_
#define ENGINE_FRAME_STAT_H_

#include <chrono>

#include "utils/defs.h"

struct FrameStat {
    size_t frame_count{0};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point end = start;
    sec_float delta_time_real{0};  // Actual delta_time between frames (for stats)
};

#endif  // ENGINE_FRAME_STAT_H_
