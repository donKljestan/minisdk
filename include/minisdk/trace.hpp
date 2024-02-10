#pragma once
#include <iostream>

namespace minisdk {

// Lifetime/IO tracing sits in hot paths, so it is compile-time opt-in (-DMINISDK_TRACE).
// Left on, it dominates benchmark timings and buries test output.
inline constexpr bool traceEnabled =
#ifdef MINISDK_TRACE
    true;
#else
    false;
#endif

} // namespace minisdk
