#ifndef ENGINE_TRACY_H_
#define ENGINE_TRACY_H_

#ifdef TRACY_ENABLE

#    include <tracy/Tracy.hpp>
#    include <tracy/TracyOpenGL.hpp>

#else  // !TRACY_ENABLE

#    define ZoneScoped
#    define ZoneScopedN(x) (void)x

#    define TracyGpuContext
#    define TracyGpuZone(x) (void)x
#    define TracyGpuCollect

#    define FrameMarkNamed(x) (void)x

#endif  // !TRACY_ENABLE

#endif  // ENGINE_TRACY_H_
