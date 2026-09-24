// SPDX-License-Identifier: LGPL-3.0-or-later
// Allocation probe shared by the realtime tests (tst_audio_engine,
// tst_audio_mixer_engine): it interposes the allocator and counts heap calls
// made on the audio device thread. Include it in exactly one file per test
// executable. QINDATK_ALLOCATION_PROBE is defined where it works (glibc).
#pragma once

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <QtGlobal>
#if defined(__GLIBC__)
#include <pthread.h>
#endif

namespace {
std::atomic<quint64> g_probeThread{0};
std::atomic<bool> g_probing{false};
std::atomic<int> g_deviceAllocations{0};
} // namespace
#if defined(__GLIBC__)
#define QINDATK_ALLOCATION_PROBE 1
namespace {
inline void noteAllocation() noexcept
{
    if (g_probing.load(std::memory_order_relaxed)
        && quint64(pthread_self()) == g_probeThread.load(std::memory_order_relaxed))
        g_deviceAllocations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace
extern "C" {
void *__libc_malloc(size_t);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void *__libc_memalign(size_t, size_t);
void __libc_free(void *);
void *malloc(size_t size) noexcept { noteAllocation(); return __libc_malloc(size); }
void *calloc(size_t count, size_t size) noexcept { noteAllocation(); return __libc_calloc(count, size); }
void *realloc(void *pointer, size_t size) noexcept { noteAllocation(); return __libc_realloc(pointer, size); }
void free(void *pointer) noexcept { if (pointer) noteAllocation(); __libc_free(pointer); }
void *aligned_alloc(size_t alignment, size_t size) noexcept { noteAllocation(); return __libc_memalign(alignment, size); }
void *memalign(size_t alignment, size_t size) noexcept { noteAllocation(); return __libc_memalign(alignment, size); }
int posix_memalign(void **out, size_t alignment, size_t size) noexcept
{
    noteAllocation();
    void *pointer = __libc_memalign(alignment, size);
    if (!pointer) return ENOMEM;
    *out = pointer;
    return 0;
}
}
#endif
