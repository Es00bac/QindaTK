// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace QindaTK {

// AGENT-CONTRACT: single-producer/single-consumer bounded ring shared by the
// audio realtime thread and one non-realtime thread (docs/audio-engine.md).
// Storage is allocated once at construction; push/pop never allocate, lock or
// wait, and a full ring rejects the element instead of blocking the producer.
template <typename T>
class AudioSpscRing {
    static_assert(std::is_trivially_copyable_v<T>);
public:
    explicit AudioSpscRing(std::size_t capacity)
        : m_size(capacity + 1), m_items(std::make_unique<T[]>(capacity + 1)) {}
    AudioSpscRing(const AudioSpscRing &) = delete;
    AudioSpscRing &operator=(const AudioSpscRing &) = delete;

    std::size_t capacity() const noexcept { return m_size - 1; }
    std::size_t readAvailable() const noexcept
    {
        const auto head = m_head.load(std::memory_order_acquire);
        const auto tail = m_tail.load(std::memory_order_relaxed);
        return head >= tail ? head - tail : head + m_size - tail;
    }
    std::size_t writeAvailable() const noexcept
    {
        const auto head = m_head.load(std::memory_order_relaxed);
        const auto tail = m_tail.load(std::memory_order_acquire);
        return (tail > head ? tail - head : tail + m_size - head) - 1;
    }
    bool push(const T &item) noexcept { return pushSpan(&item, 1); }
    bool pop(T &item) noexcept { return popSpan(&item, 1) == 1; }

    // All or nothing: a span that does not fit is rejected whole.
    bool pushSpan(const T *items, std::size_t count) noexcept
    {
        if (count > writeAvailable()) return false;
        auto head = m_head.load(std::memory_order_relaxed);
        const std::size_t first = std::min(count, m_size - head);
        std::copy_n(items, first, m_items.get() + head);
        std::copy_n(items + first, count - first, m_items.get());
        head = (head + count) % m_size;
        m_head.store(head, std::memory_order_release);
        return true;
    }
    // Pops up to `count` items; returns how many were copied.
    std::size_t popSpan(T *items, std::size_t count) noexcept
    {
        count = std::min(count, readAvailable());
        auto tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t first = std::min(count, m_size - tail);
        std::copy_n(m_items.get() + tail, first, items);
        std::copy_n(m_items.get(), count - first, items + first);
        m_tail.store((tail + count) % m_size, std::memory_order_release);
        return count;
    }

private:
    const std::size_t m_size;
    std::unique_ptr<T[]> m_items;
    alignas(64) std::atomic<std::size_t> m_head{0};
    alignas(64) std::atomic<std::size_t> m_tail{0};
};

} // namespace QindaTK
