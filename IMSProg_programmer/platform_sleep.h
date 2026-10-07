/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IMSPROG_PLATFORM_SLEEP_H
#define IMSPROG_PLATFORM_SLEEP_H
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
/* Round up: never shorten a device's required delay. Windows is not a
 * real-time scheduler; very short requested waits may take milliseconds. */
static inline void imsprog_usleep(unsigned long usec)
{
    Sleep((DWORD)(usec / 1000UL + (usec % 1000UL != 0)));
}
static inline void imsprog_sleep(unsigned int seconds)
{
    while (seconds--) Sleep(1000);
}
#else
#include <unistd.h>
static inline void imsprog_usleep(unsigned long usec) { usleep(usec); }
static inline void imsprog_sleep(unsigned int seconds) { sleep(seconds); }
#endif
#endif
