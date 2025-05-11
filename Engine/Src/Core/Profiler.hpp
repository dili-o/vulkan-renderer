#pragma once

// Tracy Defines
#if defined(HELIX_WITH_TRACY)
#include <tracy/Tracy.hpp>

#define HELIX_PROFILER_COLOR_DEFAULT 0x000000
#define HELIX_PROFILER_COLOR_WAIT 0xff0000
#define HELIX_PROFILER_COLOR_SUBMIT 0x0000ff
#define HELIX_PROFILER_COLOR_PRESENT 0x00ff00
#define HELIX_PROFILER_COLOR_CREATE 0xff6600
#define HELIX_PROFILER_COLOR_DESTROY 0xffa500
#define HELIX_PROFILER_COLOR_BARRIER 0xffffff

#define HELIX_PROFILER_FUNCTION() ZoneScoped
#define HELIX_PROFILER_FUNCTION_COLOR(color) ZoneScopedC(color)
#define HELIX_PROFILER_ZONE(name, color)                                       \
  {                                                                            \
    ZoneScopedC(color);                                                        \
    ZoneName(name, strlen(name));
#define HELIX_PROFILER_ZONE_END() }
#define HELIX_PROFILER_THREAD(name) tracy::SetThreadName(name)
#define HELIX_PROFILER_FRAME(name) FrameMarkNamed(name)
#define HELIX_PROFILER_ZONE_TEXT(text, length) ZoneText(text, length)

#else
#define HELIX_PROFILER_COLOR_DEFAULT
#define HELIX_PROFILER_COLOR_WAIT
#define HELIX_PROFILER_COLOR_SUBMIT
#define HELIX_PROFILER_COLOR_PRESENT
#define HELIX_PROFILER_COLOR_CREATE
#define HELIX_PROFILER_COLOR_DESTROY
#define HELIX_PROFILER_COLOR_BARRIER

#define HELIX_PROFILER_FUNCTION()
#define HELIX_PROFILER_FUNCTION_COLOR(color)
#define HELIX_PROFILER_ZONE(name, color) {
#define HELIX_PROFILER_ZONE_END() }
#define HELIX_PROFILER_THREAD(name)
#define HELIX_PROFILER_FRAME(name)
#define HELIX_PROFILER_ZONE_TEXT(text, length)
#endif
