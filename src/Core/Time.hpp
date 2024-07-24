#pragma once

#include "Platform.hpp"

namespace Helix {
    namespace Time {
        void                         service_init();                // Needs to be called once at startup.
        void                         service_shutdow();           // Needs to be called at shutdown.

        i64                          now();                         // Get current time ticks.

        f64                          microseconds(i64 time);  // Get microseconds from time ticks
        f64                          milliseconds(i64 time);  // Get milliseconds from time ticks
        f64                          seconds(i64 time);       // Get seconds from time ticks

        i64                          from(i64 starting_time); // Get time difference from start to current time.
        f64                          from_microseconds(i64 starting_time); // Convenience method.
        f64                          from_milliseconds(i64 starting_time); // Convenience method.
        f64                          from_seconds(i64 starting_time);      // Convenience method.

        f64                          delta_seconds(i64 starting_time, i64 ending_time);
        f64                          delta_microseconds(i64 starting_time, i64 ending_time);
    } // namespace Time
} // namespace Helix