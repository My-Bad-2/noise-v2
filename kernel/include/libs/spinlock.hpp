#pragma once

#include <atomic>
#include "arch.hpp"

namespace kernel {
namespace __details {
enum class LockType : uint8_t {
    SpinlockSpin,
    SpinlockIrq,
    Irq,
    RwLock,
};

template <LockType type>
class BaseLock;

template <>
class BaseLock<LockType::SpinlockSpin> {
   public:
    constexpr BaseLock() : next_ticket(0), serving_ticket(0) {}

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    BaseLock(const BaseLock&) = delete;
    BaseLock(BaseLock&&)      = delete;

    BaseLock& operator=(const BaseLock&) = delete;
    BaseLock& operator=(BaseLock&&)      = delete;

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
class BaseLock<LockType::Irq> {
   public:
    constexpr BaseLock() : interrupts(false) {}

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    BaseLock(const BaseLock&) = delete;
    BaseLock(BaseLock&&)      = delete;

    BaseLock& operator=(const BaseLock&) = delete;
    BaseLock& operator=(BaseLock&&)      = delete;

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
class BaseLock<LockType::SpinlockIrq> {
   public:
    constexpr BaseLock() : internal_lock(), irq_lock() {}

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    BaseLock(const BaseLock&) = delete;
    BaseLock(BaseLock&&)      = delete;

    BaseLock& operator=(const BaseLock&) = delete;
    BaseLock& operator=(BaseLock&&)      = delete;

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
    BaseLock<LockType::SpinlockSpin> internal_lock;
    BaseLock<LockType::Irq> irq_lock;
};

template <>
class BaseLock<LockType::RwLock> {
   public:
    constexpr BaseLock() : writer_lock() {
        this->readers.store(0, std::memory_order_acquire);
    }

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    BaseLock(const BaseLock&) = delete;
    BaseLock(BaseLock&&)      = delete;

    BaseLock& operator=(const BaseLock&) = delete;
    BaseLock& operator=(BaseLock&&)      = delete;

    void acquire_read() {
        while (true) {
            if (this->try_acquire_read()) {
                break;
            }

            arch::pause();
        }
    }

    void release_read() {
        this->readers.fetch_sub(1, std::memory_order_release);
    }

    void acquire_write() {
        // Lock out other writers and future readers
        this->writer_lock.lock();

        // Wait for current readers to finish
        while (this->readers.load(std::memory_order_acquire) != 0) {
            arch::pause();
        }
    }

    void release_write() {
        this->writer_lock.unlock();
    }

    bool try_acquire_read() {
        if (this->writer_lock.is_locked()) {
            return false;
        }

        this->readers.fetch_add(1, std::memory_order_acquire);

        if (this->writer_lock.is_locked()) {
            this->readers.fetch_sub(1, std::memory_order_release);
            return false;
        }

        return true;
    }

    bool try_acquire_write() {
        if (!this->writer_lock.try_lock()) {
            return false;
        }

        // We cannot wait for readers to finish.
        if (this->readers.load(std::memory_order_acquire) != 0) {
            this->writer_lock.unlock();
            return false;
        }

        return true;
    }

   private:
    BaseLock<LockType::SpinlockSpin> writer_lock;
    std::atomic<size_t> readers{0};
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

template <typename Mutex, bool Read = false, bool Write = false>
class LockGuard {
   public:
    using MutexType = Mutex;

    LockGuard() noexcept : m_mutex(nullptr), m_owns(false) {}

    explicit LockGuard(MutexType& m) : m_mutex(&m), m_owns(false) {
        if constexpr (Read) {
            this->m_mutex->acquire_read();
        } else if constexpr (Write) {
            this->m_mutex->acquire_write();
        } else {
            this->m_mutex->lock();
        }

        this->m_owns = true;
    }

    LockGuard(MutexType& m, DeferLock) noexcept : m_mutex(&m), m_owns(false) {}

    LockGuard(MutexType& m, TryToLock) : m_mutex(&m) {
        if constexpr (Read) {
            this->m_owns = m.try_acquire_read();
        } else if constexpr (Write) {
            this->m_owns = m.try_acquire_write();
        } else {
            this->m_owns = m.try_lock();
        }
    }

    LockGuard(MutexType& m, AdoptLock) noexcept : m_mutex(&m), m_owns(true) {}

    LockGuard(LockGuard&& other) noexcept : m_mutex(other.m_mutex), m_owns(other.m_owns) {
        other.m_mutex = nullptr;
        other.m_owns  = false;
    }

    ~LockGuard() {
        if (this->m_owns) {
            if constexpr (Read) {
                this->m_mutex->release_read();
            } else if constexpr (Write) {
                this->m_mutex->release_write();
            } else {
                this->m_mutex->unlock();
            }
        }
    }

    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    LockGuard& operator=(LockGuard&& other) noexcept {
        if (this != &other) {
            // If we currently own a lock, release it.
            if (this->m_owns) {
                if constexpr (Read) {
                    this->m_mutex->release_read();
                } else if constexpr (Write) {
                    this->m_mutex->release_write();
                } else {
                    this->m_mutex->unlock();
                }
            }

            // Steal resources from the other lock.
            this->m_mutex = other.m_mutex;
            this->m_owns  = other.m_owns;

            // Leave the other lock in a safe, empty state.
            other.m_mutex = nullptr;
            other.m_owns  = false;
        }

        return *this;
    }

    void lock() {
        if (!this->m_mutex) {
            return;
        }

        if (this->m_owns) {
            return;
        }

        if constexpr (Read) {
            this->m_mutex->acquire_read();
        } else if constexpr (Write) {
            this->m_mutex->acquire_write();
        } else {
            this->m_mutex->lock();
        }

        this->m_owns = true;
    }

    bool try_lock() {
        if (!this->m_mutex) {
            return false;
        }

        if (this->m_owns) {
            return false;
        }

        if constexpr (Read) {
            this->m_owns = this->m_mutex->try_acquire_read();
        } else if constexpr (Write) {
            this->m_owns = this->m_mutex->try_acquire_write();
        } else {
            this->m_owns = this->m_mutex->try_lock();
        }

        return this->m_owns;
    }

    void unlock() {
        if (!this->m_owns) {
            return;
        }

        if constexpr (Read) {
            this->m_mutex->release_read();
        } else if constexpr (Write) {
            this->m_mutex->release_write();
        } else {
            this->m_mutex->unlock();
        }

        this->m_owns = false;
    }

    MutexType* release() noexcept {
        MutexType* released_mutex = this->m_mutex;
        this->m_mutex             = nullptr;
        this->m_owns              = false;
        return released_mutex;
    }

    void swap(LockGuard& other) noexcept {
        std::swap(this->m_mutex, other.m_mutex);
        std::swap(this->m_owns, other.m_owns);
    }

    bool owns_lock() const noexcept {
        return this->m_owns;
    }

    explicit operator bool() const noexcept {
        return this->m_owns;
    }

    MutexType* mutex() const noexcept {
        return this->m_mutex;
    }

   private:
    MutexType* m_mutex;
    bool m_owns;
};
}  // namespace __details

template <typename Mutex>
struct LockGuard : public __details::LockGuard<Mutex, false, false> {
    using Base = __details::LockGuard<Mutex, false, false>;
    using Base::Base;
};

template <typename Mutex>
LockGuard(Mutex&) -> LockGuard<Mutex>;

template <typename Mutex>
struct ReadGuard : public __details::LockGuard<Mutex, true, false> {
    using Base = __details::LockGuard<Mutex, true, false>;
    using Base::Base;
};

template <typename Mutex>
ReadGuard(Mutex&) -> ReadGuard<Mutex>;

template <typename Mutex>
struct WriteGuard : public __details::LockGuard<Mutex, false, true> {
    using Base = __details::LockGuard<Mutex, false, true>;
    using Base::Base;
};

template <typename Mutex>
WriteGuard(Mutex&) -> WriteGuard<Mutex>;

using SpinLock      = __details::BaseLock<__details::LockType::SpinlockSpin>;
using IrqLock       = __details::BaseLock<__details::LockType::SpinlockIrq>;
using InterruptLock = __details::BaseLock<__details::LockType::Irq>;
using RWLock        = __details::BaseLock<__details::LockType::RwLock>;
}  // namespace kernel