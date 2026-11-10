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
/* Never shorten a device's required delay.
 * Sleep() runs on the system timer tick (15.6 ms by default), so even
 * Sleep(1) takes ~15.6 ms. Short waits (the per-page WREN delay, NAND busy
 * polling) therefore spin on the performance counter instead; only long
 * waits yield the CPU with Sleep(), rounded up to whole milliseconds. */
static inline void imsprog_usleep(unsigned long usec)
{
    LARGE_INTEGER freq, start, now;
    if (!usec) return;
    if (usec > 2000UL || !QueryPerformanceFrequency(&freq) ||
        !QueryPerformanceCounter(&start)) {
        Sleep((DWORD)(usec / 1000UL + (usec % 1000UL != 0)));
        return;
    }
    do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while ((unsigned long long)(now.QuadPart - start.QuadPart) * 1000000ULL <
             (unsigned long long)usec * (unsigned long long)freq.QuadPart);
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
