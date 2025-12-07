#pragma once

#include <atomic>
#include "arch.hpp"

namespace kernel {

namespace __details {

/**
 * @brief Enumeration of lock types supported by the `Spinlock` template.
 *
 * Different specializations encode different policies:
 *  - `SpinlockSpin`: pure spinning, no IRQ fiddling.
 *  - `SpinlockIrq`: spin + interrupt masking while held.
 */
enum class LockType : uint8_t {
    SpinlockSpin,
    SpinlockIrq,
};

/**
 * @brief Primary spinlock template, specialized by LockType.
 *
 * Additional lock strategies (e.g. backoff, MCS) could be implemented
 * as further specializations without affecting callers.
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
     * Design choice:
     *  - Use a monotonically-increasing ticket counter instead of a simple
     *    test-and-set flag so that high-contention scenarios remain fair
     *    (FIFO) and cache-friendly.
     */
    void lock() {
        // Reserve our ticket number atomically.
        size_t ticket = this->next_ticket.fetch_add(1, std::memory_order_relaxed);

        // Spin until our ticket is the one being served.
        while (this->serving_ticket.load(std::memory_order_acquire) != ticket) {
            // Hint to CPU that we are in a tight spin loop (SMT-friendly).
            arch::pause();
        }
    }

    /**
     * @brief Release the lock.
     *
     * Increments the `serving_ticket` counter, allowing the next waiting
     * ticket holder (if any) to acquire the lock.
     *
     * Returning `false` on an already-unlocked lock is mainly useful for
     * debug/invariants; callers normally assume well-formed usage.
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
     * Rationale:
     *  - Allows callers to build non-blocking algorithms or implement
     *    opportunistic fast paths where spinning would be undesirable.
     */
    bool try_lock() {
        if (this->is_locked()) {
            return false;
        }

        this->lock();
        return true;
    }

    /**
     * @brief Check whether the lock is currently held by any thread.
     *
     * This function is non-atomic in the sense that the result may
     * become stale immediately after it is computed, but it is useful
     * for diagnostics or building higher-level operations.
     *
     * Interpretation:
     *  - `next_ticket == serving_ticket` means no one holds the lock.
     *  - Any difference means at least one waiter/owner exists.
     */
    bool is_locked() {
        size_t curr = this->serving_ticket.load(std::memory_order_relaxed);
        size_t next = this->next_ticket.load(std::memory_order_relaxed);

        return curr != next;
    }

   private:
    /// Next ticket number to assign; monotonically increasing.
    std::atomic_size_t next_ticket;
    /// Ticket number currently being served (i.e. owning the lock).
    std::atomic_size_t serving_ticket;
};

/**
 * @brief Spinlock that also saves/restores interrupt state.
 *
 * Use this when protecting data structures that are touched from both
 * normal and interrupt context on the same CPU. The lock:
 *  - Captures the current IF flag before locking.
 *  - Disables interrupts while held (if they were enabled).
 *  - Restores the previous interrupt state on unlock.
 *
 * This keeps critical sections atomic with respect to IRQ handlers
 * without placing extra requirements on callers.
 */
template <>
class Spinlock<LockType::SpinlockIrq> {
   public:
    constexpr Spinlock() : internal_lock(), interrupts(false) {}

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

        this->internal_lock.lock();
    }

    bool unlock() {
        if (!this->internal_lock.unlock()) {
            return false;
        }

        // Only re-enable interrupts if we disabled them on entry.
        if (this->interrupts) {
            arch::enable_interrupts();
        }

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
    bool interrupts;
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
 *
 * Motivation:
 *  - Lets callers defer acquisition until after some non-trivial setup.
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
 *
 * Motivation:
 *  - Integrates non-blocking lock acquisition with RAII management.
 */
struct TryToLock {
    explicit TryToLock() = default;
};

/**
 * @brief Tag type: assume the mutex is already locked by the current context.
 *
 * Use when you have manually locked the mutex before constructing the guard.
 * This is useful when migrating legacy code to RAII-style locking.
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
 * guard currently owns the lock, making early returns and exceptions
 * (in non-kernel code) safe with respect to lock lifetime.
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
     * This is the most common pattern: acquire on construction and
     * release on scope exit.
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
     * This is useful when some setup must happen before acquiring.
     */
    LockGuard(MutexType& m, DeferLock) noexcept : m_mutex(&m), m_owns(false) {}

    /**
     * @brief Construct and attempt to acquire the lock without blocking.
     *
     * Ownership is recorded only if `try_lock()` succeeds.
     * This enables opportunistic acquisition without mandatory spinning.
     */
    LockGuard(MutexType& m, TryToLock) : m_mutex(&m), m_owns(m.try_lock()) {}

    /**
     * @brief Construct a guard that assumes ownership of an already-locked mutex.
     *
     * The mutex must be locked by the current context before construction.
     * This avoids double-locking while still getting RAII semantics.
     */
    LockGuard(MutexType& m, AdoptLock) noexcept : m_mutex(&m), m_owns(true) {}

    /**
     * @brief Move-construct from another guard, transferring ownership.
     *
     * Design:
     *  - Prevents double-unlock by explicitly nulling out the source.
     */
    LockGuard(LockGuard&& other) noexcept : m_mutex(other.m_mutex), m_owns(other.m_owns) {
        other.m_mutex = nullptr;
        other.m_owns  = false;
    }

    /**
     * @brief Destructor: unlocks the mutex if currently owned.
     *
     * This is the core RAII guarantee: leaving the scope releases the lock.
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
     * This ensures we never "lose" a lock during reassignment.
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
     * No-op if the guard does not currently own the lock. This is safe
     * to call multiple times in error-handling code paths.
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
     * Useful for code that needs to hand off a locked mutex to another
     * component without triggering a double unlock.
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
     *
     * Used primarily by generic algorithms or containers manipulating locks.
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
     *
     * Exposes the raw mutex for advanced scenarios (introspection,
     * condition variables, etc.), without changing ownership.
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
 *
 * Rationale:
 *  - Keeps the public API free of the internal `__details` namespace
 *    while still allowing a single implementation.
 */
template <class Mutex>
using LockGuard = __details::LockGuard<Mutex>;

/**
 * @brief Public alias for the default ticket-based spinlock.
 */
using SpinLock = __details::Spinlock<__details::LockType::SpinlockSpin>;

/**
 * @brief Public alias for the IRQ-safe spinlock.
 */
using IrqLock = __details::Spinlock<__details::LockType::SpinlockIrq>;

}  // namespace kernel