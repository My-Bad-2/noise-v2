#pragma once

#include <atomic>
#include "arch.hpp"

namespace kernel {
namespace __details {
enum class LockType : uint8_t { SpinlockSpin, SpinlockIrq, Irq };

template <LockType type>
class Spinlock;

template <>
class Spinlock<LockType::SpinlockSpin> {
   public:
    constexpr Spinlock() : next_ticket(0), serving_ticket(0) {}

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    Spinlock(const Spinlock&) = delete;
    Spinlock(Spinlock&&)      = delete;

    Spinlock& operator=(const Spinlock&) = delete;
    Spinlock& operator=(Spinlock&&)      = delete;

    void lock() {
        // Reserve our ticket number atomically.
        size_t ticket = this->next_ticket.fetch_add(1, std::memory_order_relaxed);

        // Spin until our ticket is the one being served.
        while (this->serving_ticket.load(std::memory_order_acquire) != ticket) {
            // Hint to CPU that we are in a tight spin loop (SMT-friendly).
            arch::pause();
        }
    }

    bool unlock() {
        if (!this->is_locked()) {
            // Nothing to unlock; caller did not currently own the lock.
            return false;
        }

        size_t curr = this->serving_ticket.load(std::memory_order_relaxed);
        // Hand off the lock to the next ticket holder.
        this->serving_ticket.store(curr + 1, std::memory_order_release);

        return true;
    }

    bool try_lock() {
        if (this->is_locked()) {
            return false;
        }

        this->lock();
        return true;
    }

    bool is_locked() {
        size_t curr = this->serving_ticket.load(std::memory_order_relaxed);
        size_t next = this->next_ticket.load(std::memory_order_relaxed);

        return curr != next;
    }

   private:
    std::atomic_size_t next_ticket;
    std::atomic_size_t serving_ticket;
};

template <>
class Spinlock<LockType::Irq> {
   public:
    constexpr Spinlock() : interrupts(false) {}

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    Spinlock(const Spinlock&) = delete;
    Spinlock(Spinlock&&)      = delete;

    Spinlock& operator=(const Spinlock&) = delete;
    Spinlock& operator=(Spinlock&&)      = delete;

    void lock() {
        // Record whether interrupts were enabled when entering.
        this->interrupts = arch::interrupt_status();

        if (this->interrupts) {
            // Prevent IRQ handlers from racing with this critical section.
            arch::disable_interrupts();
        }
    }

    bool unlock() {
        // Only re-enable interrupts if we disabled them on entry.
        if (this->interrupts) {
            arch::enable_interrupts();
        }

        return true;
    }

    bool try_lock() {
        this->lock();
        return true;
    }

   private:
    bool interrupts;
};

template <>
class Spinlock<LockType::SpinlockIrq> {
   public:
    constexpr Spinlock() : internal_lock(), irq_lock() {}

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    Spinlock(const Spinlock&) = delete;
    Spinlock(Spinlock&&)      = delete;

    Spinlock& operator=(const Spinlock&) = delete;
    Spinlock& operator=(Spinlock&&)      = delete;

    void lock() {
        this->irq_lock.lock();
        this->internal_lock.lock();
    }

    bool unlock() {
        if (!this->internal_lock.unlock()) {
            return false;
        }

        this->irq_lock.unlock();

        return true;
    }

    bool try_lock() {
        if (this->internal_lock.is_locked()) {
            return false;
        }

        this->lock();
        return true;
    }

   private:
    Spinlock<LockType::SpinlockSpin> internal_lock;
    Spinlock<LockType::Irq> irq_lock;
};

struct DeferLock {
    explicit DeferLock() = default;
};

struct TryToLock {
    explicit TryToLock() = default;
};

struct AdoptLock {
    explicit AdoptLock() = default;
};

constexpr DeferLock defer_lock{};
constexpr TryToLock try_to_lock{};
constexpr AdoptLock adopt_lock{};

template <typename Mutex>
class LockGuard {
   public:
    using MutexType = Mutex;

    LockGuard() noexcept : m_mutex(nullptr), m_owns(false) {}

    explicit LockGuard(MutexType& m) : m_mutex(&m), m_owns(false) {
        m_mutex->lock();
        m_owns = true;
    }

    LockGuard(MutexType& m, DeferLock) noexcept : m_mutex(&m), m_owns(false) {}

    LockGuard(MutexType& m, TryToLock) : m_mutex(&m), m_owns(m.try_lock()) {}

    LockGuard(MutexType& m, AdoptLock) noexcept : m_mutex(&m), m_owns(true) {}

    LockGuard(LockGuard&& other) noexcept : m_mutex(other.m_mutex), m_owns(other.m_owns) {
        other.m_mutex = nullptr;
        other.m_owns  = false;
    }

    ~LockGuard() {
        if (m_owns) {
            m_mutex->unlock();
        }
    }

    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    LockGuard& operator=(LockGuard&& other) noexcept {
        if (this != &other) {
            // If we currently own a lock, release it.
            if (m_owns) {
                m_mutex->unlock();
            }

            // Steal resources from the other lock.
            m_mutex = other.m_mutex;
            m_owns  = other.m_owns;

            // Leave the other lock in a safe, empty state.
            other.m_mutex = nullptr;
            other.m_owns  = false;
        }

        return *this;
    }

    void lock() {
        if (!m_mutex) {
            return;
        }

        if (m_owns) {
            return;
        }

        m_mutex->lock();
        m_owns = true;
    }

    bool try_lock() {
        if (!m_mutex) {
            return false;
        }

        if (m_owns) {
            return false;
        }

        m_owns = m_mutex->try_lock();
        return m_owns;
    }

    void unlock() {
        if (!m_owns) {
            return;
        }

        m_mutex->unlock();
        m_owns = false;
    }

    MutexType* release() noexcept {
        MutexType* released_mutex = m_mutex;
        m_mutex                   = nullptr;
        m_owns                    = false;
        return released_mutex;
    }

    void swap(LockGuard& other) noexcept {
        std::swap(m_mutex, other.m_mutex);
        std::swap(m_owns, other.m_owns);
    }

    bool owns_lock() const noexcept {
        return m_owns;
    }

    explicit operator bool() const noexcept {
        return m_owns;
    }

    MutexType* mutex() const noexcept {
        return m_mutex;
    }

   private:
    /// Pointer to the managed mutex, or nullptr if none.
    MutexType* m_mutex;
    /// Whether this guard currently owns (holds) the lock.
    bool m_owns;
};

template <class Mutex>
LockGuard(Mutex&) -> LockGuard<Mutex>;
}  // namespace __details

template <class Mutex>
using LockGuard = __details::LockGuard<Mutex>;

using SpinLock      = __details::Spinlock<__details::LockType::SpinlockSpin>;
using IrqLock       = __details::Spinlock<__details::LockType::SpinlockIrq>;
using InterruptLock = __details::Spinlock<__details::LockType::Irq>;
}  // namespace kernel