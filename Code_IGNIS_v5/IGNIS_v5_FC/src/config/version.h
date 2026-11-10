// =============================================================================
// IGNIS v5: version.h
// Firmware identity, logged at boot, written to the SD header, shown on the
// dashboard. The authoritative "what is flying" value is the SHA-256 of the
// running app partition, computed at boot (see main sketch). The git hash
// below is informational and comes from tools/gen_build_info.py.
// =============================================================================
#pragma once

#define IGNIS_FW_NAME     "IGNIS-v5-FC"
#define IGNIS_FW_VERSION  "0.2.0-prelim"
// This variant: one parachute event, CH2 = backup charge, retry if the e-match stays intact.
#define IGNIS_FW_VARIANT  "SINGLE-DEPLOY"

#if defined(__has_include)
#if __has_include("build_info.h")
#include "build_info.h"   // generated, gitignored: defines IGNIS_GIT_HASH
#endif
#endif
#ifndef IGNIS_GIT_HASH
#define IGNIS_GIT_HASH "nogit"
#endif
