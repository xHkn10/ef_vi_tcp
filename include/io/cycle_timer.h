#pragma once

#include <bits/this_thread_sleep.h>

#include "x86intrin.h"
#include "types.h"

namespace io {
    struct cycle_timer {
        inline static u64 ack_timeout_cycles = 0; // 100ms
        inline static u64 cycles_per_ms = 0;

        static void calibrate() {
            using namespace std::chrono_literals;
            u64 start = __rdtsc();
            std::this_thread::sleep_for(10ms);
            u64 end = __rdtsc();

            cycles_per_ms = (end - start) / 10;
            ack_timeout_cycles = cycles_per_ms * 100;
        }
        static u64 now() {
            return __rdtsc();
        }
    };
}
