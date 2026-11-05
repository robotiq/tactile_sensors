/* Copyright (c) 2026 Robotiq, Inc.
 *
 * Licensed under the BSD-3-Clause license; see LICENSE for details.
 */

/*! \brief The C-library glue a freestanding SDK build needs, backed by the
 * ThreadX tick. Part of the link fixture, and the reference for what firmware
 * has to provide.
 *
 * std::chrono::steady_clock is the one that bites. libstdc++ backs it with a
 * libc call, and WHICH call is a property of the multilib: arm-none-eabi 13.x
 * for v8-m.main uses gettimeofday(), not clock_gettime(). Neither is provided
 * on a freestanding target, and --specs=nosys.specs stubs the missing one to
 * a silent failure rather than a link error — so an unimplemented backing
 * leaves steady_clock frozen, every SDK timeout broken, and activate() looking
 * hung. The CI job asserts which symbol this toolchain requires; both are
 * implemented here so the fixture survives a toolchain that switches.
 */

#include <sys/time.h>
#include <time.h>

#include "tx_api.h"

/* The tick is coarse (TX_TIMER_TICKS_PER_SECOND), which steady_clock does not
 * mind — it requires monotonicity, not resolution. */
static unsigned long long elapsed_microseconds(void)
{
   return (unsigned long long)tx_time_get() * (1000000ull / TX_TIMER_TICKS_PER_SECOND);
}

int _gettimeofday(struct timeval* tv, void* tz)
{
   const unsigned long long us = elapsed_microseconds();
   (void)tz;
   tv->tv_sec = (time_t)(us / 1000000ull);
   tv->tv_usec = (suseconds_t)(us % 1000000ull);
   return 0;
}

int clock_gettime(clockid_t clock_id, struct timespec* tp)
{
   const unsigned long long us = elapsed_microseconds();
   (void)clock_id;
   tp->tv_sec = (time_t)(us / 1000000ull);
   tp->tv_nsec = (long)((us % 1000000ull) * 1000ull);
   return 0;
}

/* newlib's heap is not reentrant, and the SDK allocates from more than one
 * thread (the exchange task and the application). Firmware should raise these
 * to a real mutex; a link fixture only has to define them. */
void __malloc_lock(struct _reent* reent)
{
   (void)reent;
}

void __malloc_unlock(struct _reent* reent)
{
   (void)reent;
}
