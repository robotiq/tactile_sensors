// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Build-configuration macros, defaulted for consumers compiling
//! against the headers without the CMake target (which defines both itself):
//!   GRIPPERS_HOSTED — 1 on a hosted runtime (std::thread, iostream, a wall
//!   clock); 0 on a freestanding target. Gates only the hosted conveniences:
//!   the wait helpers that sleep on the default Platform.
//!   GRIPPERS_BUILD_DEFAULT_SERIAL — 1 when the libserialport-backed
//!   DefaultSerial and the ConnectionConfig constructors that build it are
//!   compiled in. Targets without it inject their own Serial.

#pragma once

#ifndef GRIPPERS_HOSTED
#define GRIPPERS_HOSTED 1
#endif

// The default transport is a desktop convenience: without a hosted runtime
// there is no libserialport, so the default follows GRIPPERS_HOSTED.
#ifndef GRIPPERS_BUILD_DEFAULT_SERIAL
#define GRIPPERS_BUILD_DEFAULT_SERIAL GRIPPERS_HOSTED
#endif
