// TransitGen core — immutable object publication from the message thread to the audio thread
// (01 §7): atomic pointer swap; retired objects are freed on the message thread once the audio
// thread has acknowledged a later epoch. The audio thread never frees or allocates.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace tg {

template <typename T>
class Published {
public:
    Published() = default;
    Published(const Published&) = delete;
    Published& operator=(const Published&) = delete;
    ~Published() { delete current_.load(std::memory_order_acquire); }   // owner guarantees no reader is left

    /// Message thread: makes `obj` current. The previous object is retired, not freed.
    void publish(std::unique_ptr<const T> obj)
    {
        const T* old = current_.exchange(obj.release(), std::memory_order_acq_rel);
        const uint64_t e = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (old != nullptr) retired_.emplace_back(e, std::unique_ptr<const T>(old));
        collect();
    }

    /// Audio thread, once per block: the current object. The pointer stays valid until the next
    /// acquire() on the same thread. Wait-free, allocation-free.
    const T* acquire() noexcept
    {
        const uint64_t e = epoch_.load(std::memory_order_acquire);
        const T* p = current_.load(std::memory_order_acquire);
        acked_.store(e, std::memory_order_release);
        return p;
    }

    /// Message thread (e.g. on a timer): frees retired objects the audio thread can no longer hold.
    void collect()
    {
        const uint64_t acked = acked_.load(std::memory_order_acquire);
        size_t keep = 0;
        for (size_t i = 0; i < retired_.size(); ++i)
            if (retired_[i].first > acked) retired_[keep++] = std::move(retired_[i]);
        retired_.resize(keep);
    }

    /// Message thread: the current object (for display / generate() on the UI side).
    const T* current() const noexcept { return current_.load(std::memory_order_acquire); }
    size_t   retiredCount() const noexcept { return retired_.size(); }

private:
    std::atomic<const T*> current_{nullptr};
    std::atomic<uint64_t> epoch_{0};      // number of publishes
    std::atomic<uint64_t> acked_{0};      // epoch seen by the latest acquire()
    std::vector<std::pair<uint64_t, std::unique_ptr<const T>>> retired_;   // (epoch that retired it, object)
};

} // namespace tg
