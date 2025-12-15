#pragma once

#include <stddef.h>
#include <string.h>
#include <utility>
#include <type_traits>
#include <cstdlib>
#include <iterator>

#include "libs/log.hpp"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace kernel {

template <typename T>
struct is_relocatable : std::is_trivially_copyable<T> {};

template <typename T>
inline constexpr bool UseMemOps = is_relocatable<T>::value;

template <typename T>
class Vector {
   public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;

    // --- Iterator Definitions ---
    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

   private:
    pointer start;
    pointer finish;
    pointer end_of_storage;

    pointer allocate_internal(size_type count) {
        if (unlikely(count > size_type(-1) / sizeof(T))) {
            LOG_ERROR("Allocation size overflow");
            return nullptr;
        }

        pointer ptr = static_cast<pointer>(::operator new(count * sizeof(T)));

        if (unlikely(!ptr)) {
            LOG_ERROR("Memory allocation failed (OOM)");
        }

        return ptr;
    }

    void deallocate_internal(pointer ptr) {
        if (ptr) {
            ::operator delete(ptr);
        }
    }

    size_type calculate_growth(size_type extra_needed) const {
        size_type old_cap = this->capacity();
        size_type max_cap = size_type(-1) / sizeof(T);

        if (old_cap > max_cap - old_cap / 2) {
            return max_cap;
        }

        size_type new_cap = old_cap + (old_cap >> 1);  // 1.5x growth

        if (new_cap < this->size() + extra_needed) {
            new_cap = this->size() + extra_needed;
        }

        if (new_cap < 8) {
            new_cap = 8;
        }

        return new_cap;
    }

    bool realloc_insert(size_type new_cap) {
        pointer __restrict new_start = this->allocate_internal(new_cap);

        if (!new_start) {
            return false;
        }

        pointer __restrict new_finish = new_start;
        pointer __restrict old_start  = this->start;

        if constexpr (UseMemOps<T>) {
            size_type current_size = this->size();

            if (current_size > 0) {
                memcpy(new_start, old_start, current_size * sizeof(T));
                new_finish += current_size;
            }
        } else {
            for (pointer cur = old_start; cur != this->finish; ++cur) {
                new (new_finish) T(std::move(*cur));
                ++new_finish;
            }

            for (pointer cur = old_start; cur != this->finish; ++cur) {
                cur->~T();
            }
        }

        this->deallocate_internal(old_start);
        this->start          = new_start;
        this->finish         = new_finish;
        this->end_of_storage = new_start + new_cap;

        return true;
    }

    template <typename... Args>
    void emplace_back_slow(Args&&... args) {
        size_type new_cap = this->calculate_growth(1);
        pointer new_start = this->allocate_internal(new_cap);

        if (!new_start) {
            return;
        }

        pointer new_finish = new_start;

        if constexpr (UseMemOps<T>) {
            size_type current_size = this->size();

            if (current_size > 0) {
                memcpy(new_start, this->start, current_size * sizeof(T));
            }

            new_finish += current_size;
        } else {
            for (pointer ptr = this->start; ptr != this->finish; ++ptr) {
                new (new_finish) T(std::move(*ptr));
                ++new_finish;
            }

            for (pointer ptr = this->start; ptr != this->finish; ++ptr) {
                ptr->~T();
            }
        }

        new (new_finish) T(std::forward<Args>(args)...);
        ++new_finish;

        this->deallocate_internal(this->start);
        this->start          = new_start;
        this->finish         = new_finish;
        this->end_of_storage = new_start + new_cap;
    }

   public:
    Vector() : start(nullptr), finish(nullptr), end_of_storage(nullptr) {}

    ~Vector() {
        this->clear();
        this->deallocate_internal(this->start);
    }

    Vector(const Vector& other) {
        size_type n = other.size();
        this->start = this->allocate_internal(n);

        if (!this->start) {
            this->finish         = nullptr;
            this->end_of_storage = nullptr;
            return;
        }

        this->finish         = this->start;
        this->end_of_storage = this->start + n;

        if constexpr (UseMemOps<T>) {
            if (n > 0) {
                memcpy(this->start, other.start, n * sizeof(T));
            }

            this->finish += n;
        } else {
            for (pointer ptr = other.start; ptr != other.finish; ++ptr) {
                new (this->finish) T(*ptr);
                ++this->finish;
            }
        }
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            if (this->capacity() >= other.size()) {
                this->clear();

                if constexpr (UseMemOps<T>) {
                    if (other.size() > 0) {
                        memcpy(this->start, other.start, other.size() * sizeof(T));
                    }

                    this->finish = this->start + other.size();
                } else {
                    for (pointer ptr = other.start; ptr != other.finish; ++ptr) {
                        new (this->finish) T(*ptr);
                        ++this->finish;
                    }
                }
            } else {
                pointer new_start = this->allocate_internal(other.size());

                if (!new_start) {
                    return *this;
                }

                this->clear();
                this->deallocate_internal(this->start);

                this->start          = new_start;
                this->finish         = this->start;
                this->end_of_storage = this->start + other.size();

                if constexpr (UseMemOps<T>) {
                    memcpy(this->start, other.start, other.size() * sizeof(T));
                    this->finish += other.size();
                } else {
                    for (pointer ptr = other.start; ptr != other.finish; ++ptr) {
                        new (this->finish) T(*ptr);
                        ++this->finish;
                    }
                }
            }
        }
        return *this;
    }

    Vector(Vector&& other) noexcept
        : start(other.start), finish(other.finish), end_of_storage(other.end_of_storage) {
        other.start          = nullptr;
        other.finish         = nullptr;
        other.end_of_storage = nullptr;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            this->clear();
            this->deallocate_internal(this->start);

            this->start          = other.start;
            this->finish         = other.finish;
            this->end_of_storage = other.end_of_storage;
            other.start          = nullptr;
            other.finish         = nullptr;
            other.end_of_storage = nullptr;
        }

        return *this;
    }

    void reserve(size_type n) {
        if (n > this->capacity()) {
            if (!this->realloc_insert(n)) {
                // Realloc failed.
            }
        }
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        if (likely(this->finish != this->end_of_storage)) {
            new (this->finish) T(std::forward<Args>(args)...);
            ++this->finish;
        } else {
            this->emplace_back_slow(std::forward<Args>(args)...);
        }
    }

    void push_back(const T& value) {
        this->emplace_back(value);
    }

    void push_back(T&& value) {
        this->emplace_back(std::move(value));
    }

    void pop_back() {
        if (this->finish != this->start) {
            --this->finish;

            if constexpr (!std::is_trivially_destructible_v<T>) {
                this->finish->~T();
            }
        }
    }

    void resize_no_init(size_type new_size) {
        if (new_size > this->capacity()) {
            size_type growth = this->calculate_growth(new_size - this->size());

            if (!this->realloc_insert(growth)) {
                return;
            }
        }

        if (new_size > this->size()) {
            this->finish = this->start + new_size;
        } else {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (pointer ptr = this->start + new_size; ptr != this->finish; ++ptr) ptr->~T();
            }

            this->finish = this->start + new_size;
        }
    }

    void resize(size_type new_size) {
        size_type old_size = this->size();
        this->resize_no_init(new_size);

        if (this->size() == new_size && new_size > old_size) {
            if constexpr (std::is_arithmetic_v<T>) {
                memset(this->start + old_size, 0, (new_size - old_size) * sizeof(T));
            } else {
                for (pointer ptr = this->start + old_size; ptr != this->finish; ++ptr) {
                    new (ptr) T();
                }
            }
        }
    }

    void clear() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (pointer ptr = this->start; ptr != this->finish; ++ptr) ptr->~T();
        }

        this->finish = this->start;
    }

    // --- Iterator Implementation ---

    iterator begin() noexcept {
        return this->start;
    }

    const_iterator begin() const noexcept {
        return this->start;
    }

    iterator end() noexcept {
        return this->finish;
    }

    const_iterator end() const noexcept {
        return this->finish;
    }

    const_iterator cbegin() const noexcept {
        return this->start;
    }

    const_iterator cend() const noexcept {
        return this->finish;
    }

    reverse_iterator rbegin() noexcept {
        return reverse_iterator(this->end());
    }

    const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(this->end());
    }

    reverse_iterator rend() noexcept {
        return reverse_iterator(this->begin());
    }

    const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(this->begin());
    }

    const_reverse_iterator crbegin() const noexcept {
        return const_reverse_iterator(this->end());
    }

    const_reverse_iterator crend() const noexcept {
        return const_reverse_iterator(this->begin());
    }

    // --- Data Access ---

    reference operator[](size_type index) {
        return *(this->start + index);
    }

    const_reference operator[](size_type index) const {
        return *(this->start + index);
    }

    pointer at(size_type index) {
        if (unlikely(index >= this->size())) {
            LOG_ERROR("Index out of bounds");
            return nullptr;
        }

        return this->start + index;
    }

    pointer __restrict data() {
        return this->start;
    }

    const_pointer data() const {
        return this->start;
    }

    size_type size() const {
        return this->finish - this->start;
    }

    size_type capacity() const {
        return this->end_of_storage - this->start;
    }

    bool empty() const {
        return this->start == this->finish;
    }
};

}  // namespace kernel