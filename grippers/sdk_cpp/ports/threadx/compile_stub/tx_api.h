/* Compile-only stand-in for ThreadX's tx_api.h, so CI can syntax-check the
 * port without the real RTOS. Types and signatures mirror the real API just
 * enough to compile threadx_platform.hpp. NEVER put this on a firmware
 * include path: it defines nothing that links or runs.
 */

#pragma once

typedef char CHAR;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef void VOID;

#define TX_SUCCESS 0x00u
#define TX_NO_INHERIT 0u
#define TX_WAIT_FOREVER 0xFFFFFFFFu
#define TX_NO_TIME_SLICE 0u
#define TX_AUTO_START 1u
#define TX_READY 0u
#define TX_COMPLETED 1u
#define TX_TERMINATED 2u
#define TX_TIMER_TICKS_PER_SECOND 100u

typedef struct TX_MUTEX_STRUCT
{
   ULONG tx_stub_unused;
} TX_MUTEX;

typedef struct TX_THREAD_STRUCT
{
   ULONG tx_stub_unused;
} TX_THREAD;

UINT tx_mutex_create(TX_MUTEX* mutex, CHAR* name, UINT inherit);
UINT tx_mutex_delete(TX_MUTEX* mutex);
UINT tx_mutex_get(TX_MUTEX* mutex, ULONG waitOption);
UINT tx_mutex_put(TX_MUTEX* mutex);

UINT tx_thread_create(TX_THREAD* thread,
                      CHAR* name,
                      VOID (*entry)(ULONG),
                      ULONG entryInput,
                      VOID* stackStart,
                      ULONG stackSize,
                      UINT priority,
                      UINT preemptThreshold,
                      ULONG timeSlice,
                      UINT autoStart);
UINT tx_thread_delete(TX_THREAD* thread);
UINT tx_thread_terminate(TX_THREAD* thread);
UINT tx_thread_sleep(ULONG timerTicks);
UINT tx_thread_info_get(TX_THREAD* thread,
                        CHAR** name,
                        UINT* state,
                        ULONG* runCount,
                        UINT* priority,
                        UINT* preemptionThreshold,
                        ULONG* timeSlice,
                        TX_THREAD** nextThread,
                        TX_THREAD** suspendedThread);
