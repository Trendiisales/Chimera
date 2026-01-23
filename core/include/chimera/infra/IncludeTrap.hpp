#pragma once

// HARD ENFORCEMENT: Forbidden include patterns
// These will cause compile-time errors if used

#if defined(__has_include)
  #if __has_include("Clock.hpp")
    #error "Forbidden include: use chimera/infra/Clock.hpp"
  #endif
  #if __has_include("PnL.hpp")
    #error "Forbidden include: use chimera/pnl/PnL.hpp"
  #endif
  #if __has_include("events.hpp")
    #error "Forbidden include: use chimera/causal/events.hpp or chimera/core/Events.hpp"
  #endif
  #if __has_include("replay.hpp")
    #error "Forbidden include: use chimera/causal/replay.hpp"
  #endif
#endif

// Additional enforcement: Check that CHIMERA_INCLUDE_ROOT is defined
#ifndef CHIMERA_INCLUDE_ROOT
#error "All files must be compiled via Chimera CMake. Direct compilation is forbidden."
#endif
