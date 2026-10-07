#pragma once

// The one include every former site of the generated constants header uses (Phase 268 plan 01, G268-2).
//
// - Default build (no RL_SPEC option): the generated enhancement header engine/sim/rl_policy_constants.h
//   is compiled unchanged. While it lacks the C-A marker RL_HEADER_CA_VERSION, enhancement's hand-written
//   C-A supplement is compiled after it.
// - -DRL_SPEC=<spec> (CMake cache option, engine/CMakeLists.txt): the build defines RL_SPEC_HEADER as the
//   string "sim/rl_policy_constants.<spec>.h" and this shim compiles that header instead. A spec header
//   that does not carry the C-A field set (RL_HEADER_CA_VERSION) is refused at compile time.
// Either way the compile-time pins of the C-A field set (rl_header_contract.hpp) come last.

#ifdef RL_SPEC_HEADER
#include RL_SPEC_HEADER
#ifndef RL_HEADER_CA_VERSION
#error "RL_SPEC_HEADER names a constants header without the C-A field set (RL_HEADER_CA_VERSION undefined): regenerate it or add the C-A fields (contract C-A)"
#endif
#else
#include "sim/rl_policy_constants.h"
#ifndef RL_HEADER_CA_VERSION
#include "sim/rl_policy_constants_ca.enhancement.h"
#endif
#endif
#include "sim/rl_header_contract.hpp"
