/**
 * @file spinlock.hpp
 * @brief Simple ticket-based spinlock and RAII lock guard utilities.
 *
 * This header provides a small synchronization primitive::
 *
 *  - A ticket-based spinlock implementation that avoids starvation and
 *    preserves FIFO lock acquisition order.
 *  - A configurable `LockGuard` RAII wrapper that works with any mutex-like
 *    type exposing `lock`, `try_lock`, and `unlock`.
 *
 * The implementation is header-only and uses `std::atomic` for safety.
 */

#pragma once

#include <atomic>
#include "arch.hpp"

namespace kernel {

namespace __details {

/**
 * @brief Enumeration of lock types supported by the `Spinlock` template.
 */
enum class LockType : uint8_t {
    SpinlockSpin,
};

/**
 * @brief Primary spinlock template, specialized by LockType.
 *
 * Additional lock strategies (e.g. backoff, MCS) could be implemented
 * as further specializations.
 */
template <LockType type>
class Spinlock;

/**
 * @brief Ticket-based spinlock implementation.
 *
 * Each caller atomically fetches a "ticket" number and then spins until
 * its ticket is the one currently being served. This guarantees FIFO
 * ordering and avoids starvation that can occur with simple TAS locks.
 *
 * The API is intentionally minimal and mutex-like:
 *  - `lock()` blocks (spins) until the lock is acquired.
 *  - `try_lock()` returns immediately if the lock is not available.
 *  - `unlock()` releases the lock and returns whether it actually owned it.
 */
template <>
class Spinlock<LockType::SpinlockSpin> {
   public:
    /// Construct an unlocked spinlock (initial ticket counters are 0).
    constexpr Spinlock() : next_ticket(0), serving_ticket(0) {}

    // Spinlocks are non-copyable and non-movable to avoid accidental sharing.
    Spinlock(const Spinlock&) = delete;
    Spinlock(Spinlock&&)      = delete;

    Spinlock& operator=(const Spinlock&) = delete;
    Spinlock& operator=(Spinlock&&)      = delete;

    /**
     * @brief Acquire the lock, spinning until it becomes available.
     *
     * Each caller obtains a unique ticket and then waits until its ticket
     * is equal to `serving_ticket`. While waiting, the function issues
     * `arch::pause()` to behave better on SMT systems and reduce power.
     */
    void lock() {
        // Reserve our ticket number atomically.
        size_t ticket = this->next_ticket.fetch_add(1, std::memory_order_relaxed);

        // Spin until our ticket is the one being served.
        while (this->serving_ticket.load(std::memory_order_acquire) != ticket) {
            arch::pause();  // Hint to CPU that we are in a tight spin loop.
        }
    }

    /**
     * @brief Release the lock.
     *
     * Increments the `serving_ticket` counter, allowing the next waiting
     * ticket holder (if any) to acquire the lock.
     *
     * @return true if the lock was held and is now released, false if it
     *         was already unlocked when `unlock()` was called.
     */
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

    /**
     * @brief Try to acquire the lock without blocking.
     *
     * If the lock is currently held, this function returns immediately
     * without spinning.
     *
     * @return true if the lock was acquired, false otherwise.
     */
    bool try_lock() {
        if (this->is_locked()) {
            return false;
        }

        this->lock();
        return true;
    }

   private:
    /**
     * @brief Check whether the lock is currently held by any thread.
     *
     * This function is non-atomic in the sense that the result may
     * become stale immediately after it is computed, but it is useful
     * for diagnostics or building higher-level operations.
     *
     * @return true if at least one thread holds the lock, false otherwise.
     */
    bool is_locked() {
        size_t curr = this->serving_ticket.load(std::memory_order_relaxed);
        size_t next = this->next_ticket.load(std::memory_order_relaxed);

        return curr != next;
    }

    /// Next ticket number to assign; monotonically increasing.
    std::atomic_size_t next_ticket;
    /// Ticket number currently being served (i.e. owning the lock).
    std::atomic_size_t serving_ticket;
};

/**
 * @brief Tag type: construct a LockGuard without immediately locking.
 *
 * Use as:
 * @code
 * SpinLock m;
 * LockGuard<SpinLock> guard(m, defer_lock);
 * // ... do some work ...
 * guard.lock();
 * @endcode
 */
struct DeferLock {
    explicit DeferLock() = default;
};

/**
 * @brief Tag type: attempt to lock without blocking.
 *
 * Use as:
 * @code
 * SpinLock m;
 * LockGuard<SpinLock> guard(m, try_to_lock);
 * if (!guard) {
 *   // lock acquisition failed
 * }
 * @endcode
 */
struct TryToLock {
    explicit TryToLock() = default;
};

/**
 * @brief Tag type: assume the mutex is already locked by the current context.
 *
 * Use when you have manually locked the mutex before constructing the guard.
 */
struct AdoptLock {
    explicit AdoptLock() = default;
};

/// Tag constant for deferred locking.
constexpr DeferLock defer_lock{};
/// Tag constant for try-lock semantics.
constexpr TryToLock try_to_lock{};
/// Tag constant for adopting an already-held lock.
constexpr AdoptLock adopt_lock{};

/**
 * @brief Generic RAII lock guard.
 *
 * This class mimics `std::unique_lock` in spirit, but exposes only a
 * subset of its functionality. It manages a pointer to a mutex-like
 * object and a boolean flag indicating ownership.
 *
 * The mutex type must implement:
 *  - `void lock()`
 *  - `void unlock()`
 *  - `bool try_lock()`
 *
 * The guard ensures that `unlock()` is called in its destructor if the
 * guard currently owns the lock.
 *
 * @tparam Mutex Mutex/lockable type to manage.
 */
template <typename Mutex>
class LockGuard {
   public:
    using MutexType = Mutex;

    /// Construct an empty guard not associated with any mutex.
    LockGuard() noexcept : m_mutex(nullptr), m_owns(false) {}

    /**
     * @brief Construct and immediately lock the given mutex.
     *
     * @param m Mutex to lock.
     */
    explicit LockGuard(MutexType& m) : m_mutex(&m), m_owns(false) {
        m_mutex->lock();
        m_owns = true;
    }

    /**
     * @brief Construct without locking, using the DeferLock tag.
     *
     * Caller is responsible for calling `lock()` explicitly later.
     */
    LockGuard(MutexType& m, DeferLock) noexcept : m_mutex(&m), m_owns(false) {}

    /**
     * @brief Construct and attempt to acquire the lock without blocking.
     *
     * Ownership is recorded only if `try_lock()` succeeds.
     */
    LockGuard(MutexType& m, TryToLock) : m_mutex(&m), m_owns(m.try_lock()) {}

    /**
     * @brief Construct a guard that assumes ownership of an already-locked mutex.
     *
     * The mutex must be locked by the current context before construction.
     */
    LockGuard(MutexType& m, AdoptLock) noexcept : m_mutex(&m), m_owns(true) {}

    /**
     * @brief Move-construct from another guard, transferring ownership.
     */
    LockGuard(LockGuard&& other) noexcept : m_mutex(other.m_mutex), m_owns(other.m_owns) {
        other.m_mutex = nullptr;
        other.m_owns  = false;
    }

    /**
     * @brief Destructor: unlocks the mutex if currently owned.
     */
    ~LockGuard() {
        if (m_owns) {
            m_mutex->unlock();
        }
    }

    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    /**
     * @brief Move-assign from another guard, transferring ownership.
     *
     * If this guard currently owns a mutex, it will be unlocked first.
     */
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

    /**
     * @brief Acquire the lock if not already owned.
     *
     * No-op if the guard has no associated mutex or already owns the lock.
     */
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

    /**
     * @brief Try to acquire the lock without blocking.
     *
     * @return true if the lock was acquired, false otherwise.
     */
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

    /**
     * @brief Unlock the mutex if currently owned.
     *
     * No-op if the guard does not currently own the lock.
     */
    void unlock() {
        if (!m_owns) {
            return;
        }

        m_mutex->unlock();
        m_owns = false;
    }

    /**
     * @brief Release ownership without unlocking the mutex.
     *
     * This transfers raw access to the underlying mutex pointer to the
     * caller, who then becomes responsible for unlocking it.
     *
     * @return Pointer to the managed mutex, or nullptr if none.
     */
    MutexType* release() noexcept {
        MutexType* released_mutex = m_mutex;
        m_mutex                   = nullptr;
        m_owns                    = false;
        return released_mutex;
    }

    /**
     * @brief Swap the managed mutex and ownership state with another guard.
     */
    void swap(LockGuard& other) noexcept {
        std::swap(m_mutex, other.m_mutex);
        std::swap(m_owns, other.m_owns);
    }

    /**
     * @brief Check whether this guard currently owns a lock.
     */
    bool owns_lock() const noexcept {
        return m_owns;
    }

    /**
     * @brief Boolean conversion; true if the guard owns the lock.
     */
    explicit operator bool() const noexcept {
        return m_owns;
    }

    /**
     * @brief Get the underlying mutex pointer.
     */
    MutexType* mutex() const noexcept {
        return m_mutex;
    }

   private:
    /// Pointer to the managed mutex, or nullptr if none.
    MutexType* m_mutex;
    /// Whether this guard currently owns (holds) the lock.
    bool m_owns;
};

/**
 * @brief Deduction guide for `LockGuard`.
 *
 * Allows `LockGuard guard(mutex);` without specifying the template argument.
 */
template <class Mutex>
LockGuard(Mutex&) -> LockGuard<Mutex>;

}  // namespace __details

/**
 * @brief Public alias for the generic RAII lock guard.
 *
 * Example:
 * @code
 * kernel::SpinLock lock;
 * kernel::LockGuard guard(lock);
 * @endcode
 */
template <class Mutex>
using LockGuard = __details::LockGuard<Mutex>;

/**
 * @brief Public alias for the default ticket-based spinlock.
 *
 * This is the type most users of this header should use for a simple
 * non-recursive spinlock implementation.
 */
using SpinLock = __details::Spinlock<__details::LockType::SpinlockSpin>;

}  // namespace kernel