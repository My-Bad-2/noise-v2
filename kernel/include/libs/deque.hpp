#pragma once

#include <string.h>
#include <algorithm>
#include <type_traits>
#include <utility>
#include "libs/log.hpp"

namespace kernel {
const size_t BLOCK_SIZE = 8;

template <typename T>
class Deque;

template <typename T, bool IsConst = false>
class DequeIterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type        = T;
    using difference_type   = std::ptrdiff_t;
    using pointer           = typename std::conditional<IsConst, const T*, T*>::type;
    using reference         = typename std::conditional<IsConst, const T&, T&>::type;
    using map_pointer       = T**;

    T* cur;    // Pointer to the actual element
    T* first;  // Pointer to the start of the current block
    T* last;   // Pointer to the end of the current block
    T** node;  // Pointer to the map entry

    DequeIterator() : cur(nullptr), first(nullptr), last(nullptr), node(nullptr) {}

    template <bool WasConst, typename = typename std::enable_if<IsConst && !WasConst>::type>
    DequeIterator(const DequeIterator<T, WasConst>& other)
        : cur(other.cur), first(other.first), last(other.last), node(other.node) {}

    DequeIterator(T** map_node, size_t offset) : node(map_node) {
        if (this->node && *this->node) {
            this->first = *this->node;
            this->last  = this->first + BLOCK_SIZE;
            this->cur   = this->first + offset;
        } else {
            this->cur   = nullptr;
            this->first = nullptr;
            this->last  = nullptr;
        }
    }

    void set_node(T** new_node) {
        this->node  = new_node;
        this->first = *new_node;
        this->last  = this->first + BLOCK_SIZE;
    }

    reference operator*() const {
        return *this->cur;
    }

    pointer operator->() const {
        return this->cur;
    }

    DequeIterator& operator++() {
        ++this->cur;
        if (this->cur == this->last) {
            this->set_node(this->node + 1);
            this->cur = this->first;
        }
        return *this;
    }

    DequeIterator operator++(int) {
        DequeIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    DequeIterator& operator--() {
        if (this->cur == this->first) {
            this->set_node(this->node - 1);
            this->cur = this->last;
        }
        --this->cur;
        return *this;
    }

    DequeIterator operator--(int) {
        DequeIterator tmp = *this;
        --(*this);
        return tmp;
    }

    DequeIterator& operator+=(difference_type n) {
        difference_type offset = (this->cur - this->first) + n;

        if (offset >= 0 && offset < static_cast<difference_type>(BLOCK_SIZE)) {
            this->cur += n;
        } else {
            difference_type node_offset = 0;
            if (offset > 0) {
                node_offset = offset / static_cast<difference_type>(BLOCK_SIZE);
                offset %= static_cast<difference_type>(BLOCK_SIZE);
            } else {
                node_offset = -((-offset - 1) / static_cast<difference_type>(BLOCK_SIZE)) - 1;
                offset      = offset - node_offset * static_cast<difference_type>(BLOCK_SIZE);
            }

            this->set_node(this->node + node_offset);
            this->cur = this->first + offset;
        }

        return *this;
    }

    DequeIterator operator+(difference_type n) const {
        DequeIterator tmp = *this;
        return tmp += n;
    }

    DequeIterator& operator-=(difference_type n) {
        return *this += -n;
    }

    DequeIterator operator-(difference_type n) const {
        DequeIterator tmp = *this;
        return tmp -= n;
    }

    difference_type operator-(const DequeIterator& right) const {
        return (difference_type(BLOCK_SIZE) * (this->node - right.node - 1)) +
               (this->cur - this->first) + (right.last - right.cur);
    }

    bool operator==(const DequeIterator& other) const {
        return this->cur == other.cur;
    }

    bool operator!=(const DequeIterator& other) const {
        return !(*this == other);
    }

    bool operator<(const DequeIterator& other) const {
        return (this->node == other.node) ? (this->cur < other.cur) : (this->node < other.node);
    }

    bool operator>(const DequeIterator& other) const {
        return other < *this;
    }

    bool operator<=(const DequeIterator& other) const {
        return !(*this > other);
    }

    bool operator>=(const DequeIterator& other) const {
        return !(*this < other);
    }
};

template <typename T>
class Deque {
   public:
    using iterator       = DequeIterator<T, false>;
    using const_iterator = DequeIterator<T, true>;

    Deque() {
        this->element_count = 0;
        this->map_capacity  = 8;
        this->allocate_map(this->map_capacity);

        this->start_block = this->map_capacity / 2;
        this->end_block   = this->start_block;

        this->start_offset = BLOCK_SIZE / 2;
        this->end_offset   = BLOCK_SIZE / 2;

        this->ensure_block(this->start_block);
    }

    ~Deque() {
        this->clear();

        for (size_t i = 0; i < this->map_capacity; ++i) {
            if (this->map[i]) delete[] this->map[i];
        }

        delete[] this->map;
    }

    Deque(const Deque& other) {
        this->element_count = 0;
        this->map_capacity  = std::max(8ul, other.map_capacity);
        this->allocate_map(this->map_capacity);

        this->start_block  = this->map_capacity / 2;
        this->end_block    = this->start_block;
        this->start_offset = BLOCK_SIZE / 2;
        this->end_offset   = BLOCK_SIZE / 2;
        this->ensure_block(this->start_block);

        for (const auto& item : other) {
            this->push_back(item);
        }
    }

    Deque(Deque&& other) noexcept {
        this->map           = other.map;
        this->map_capacity  = other.map_capacity;
        this->start_block   = other.start_block;
        this->end_block     = other.end_block;
        this->start_offset  = other.start_offset;
        this->end_offset    = other.end_offset;
        this->element_count = other.element_count;

        other.map           = nullptr;
        other.map_capacity  = 0;
        other.element_count = 0;
        other.start_block   = 0;
        other.end_block     = 0;
        other.start_offset  = 0;
        other.end_offset    = 0;
    }

    Deque& operator=(const Deque& other) {
        if (this == &other) return *this;

        this->clear();

        for (const auto& item : other) {
            this->push_back(item);
        }

        return *this;
    }

    Deque& operator=(Deque&& other) noexcept {
        if (this == &other) return *this;

        this->clear();

        for (size_t i = 0; i < this->map_capacity; ++i) {
            if (this->map[i]) delete[] this->map[i];
        }

        delete[] this->map;

        this->map           = other.map;
        this->map_capacity  = other.map_capacity;
        this->start_block   = other.start_block;
        this->end_block     = other.end_block;
        this->start_offset  = other.start_offset;
        this->end_offset    = other.end_offset;
        this->element_count = other.element_count;

        other.map           = nullptr;
        other.map_capacity  = 0;
        other.element_count = 0;
        other.start_block   = 0;
        other.end_block     = 0;
        other.start_offset  = 0;
        other.end_offset    = 0;

        return *this;
    }

    void clear() {
        while (this->element_count > 0) this->pop_back();
    }

    size_t size() const {
        return this->element_count;
    }

    bool empty() const {
        return this->element_count == 0;
    }

    iterator begin() {
        return iterator(this->map + this->start_block, this->start_offset);
    }

    iterator end() {
        return iterator(this->map + this->end_block, this->end_offset);
    }

    const_iterator begin() const {
        return const_iterator(this->map + this->start_block, this->start_offset);
    }

    const_iterator end() const {
        return const_iterator(this->map + this->end_block, this->end_offset);
    }

    const_iterator cbegin() const {
        return this->begin();
    }

    const_iterator cend() const {
        return this->end();
    }

    const T& operator[](size_t index) const {
        return *(this->begin() + index);
    }

    T& operator[](size_t index) {
        return *(this->begin() + index);
    }

    T& front() {
        return *this->begin();
    }

    T& back() {
        return *(this->end() - 1);
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        if (this->end_offset == BLOCK_SIZE) {
            if (this->end_block + 1 == this->map_capacity) {
                this->resize_map();
            }
            this->end_block++;
            this->end_offset = 0;
            this->ensure_block(this->end_block);
        }

        new (&this->map[this->end_block][this->end_offset]) T(std::forward<Args>(args)...);
        this->end_offset++;
        this->element_count++;
    }

    void push_back(const T& value) {
        this->emplace_back(value);
    }
    void push_back(T&& value) {
        this->emplace_back(std::move(value));
    }

    template <typename... Args>
    void emplace_front(Args&&... args) {
        if (this->start_offset == 0) {
            if (this->start_block == 0) {
                this->resize_map();
            }

            this->start_block--;
            this->start_offset = BLOCK_SIZE;
            this->ensure_block(this->start_block);
        }

        this->start_offset--;
        new (&this->map[this->start_block][this->start_offset]) T(std::forward<Args>(args)...);
        this->element_count++;
    }

    void push_front(const T& value) {
        this->emplace_front(value);
    }

    void push_front(T&& value) {
        this->emplace_front(std::move(value));
    }

    void pop_back() {
        if (this->element_count == 0) {
            LOG_ERROR("Error: Deque empty during pop_back");
            return;
        }

        if (this->end_offset == 0) {
            this->end_block--;
            this->end_offset = BLOCK_SIZE;
        }

        this->end_offset--;

        this->map[this->end_block][this->end_offset].~T();
        this->element_count--;
    }

    void pop_front() {
        if (this->element_count == 0) {
            LOG_ERROR("Error: Deque empty during pop_front");
            return;
        }

        this->map[this->start_block][this->start_offset].~T();

        this->start_offset++;

        if (this->start_offset == BLOCK_SIZE) {
            this->start_block++;
            this->start_offset = 0;
        }

        this->element_count--;
    }

    template <typename... Args>
    iterator emplace(iterator pos, Args&&... args) {
        size_t index = pos - this->begin();

        if (index < this->element_count / 2) {
            this->push_front(T());

            iterator new_pos    = this->begin() + index + 1;
            iterator front_it   = this->begin();
            iterator first_elem = front_it + 1;

            this->chunked_move_forward(first_elem, new_pos, front_it);

            iterator target = new_pos - 1;
            *target         = T(std::forward<Args>(args)...);

            return target;
        } else {
            this->push_back(T());

            iterator back_it         = this->end();
            iterator src_end         = back_it - 1;
            iterator insertion_point = this->begin() + index;

            this->chunked_move_backward(insertion_point, src_end, back_it);

            *insertion_point = T(std::forward<Args>(args)...);

            return insertion_point;
        }
    }

    iterator insert(iterator pos, const T& value) {
        return this->emplace(pos, value);
    }

    iterator erase(iterator pos) {
        size_t index     = pos - this->begin();
        iterator next_it = pos + 1;

        if (index < this->element_count / 2) {
            if (pos != this->begin()) {
                this->chunked_move_backward(this->begin(), pos, next_it);
            }

            this->pop_front();

            return this->begin() + index;
        } else {
            if (next_it != this->end()) {
                this->chunked_move_forward(next_it, this->end(), pos);
            }

            this->pop_back();

            return this->begin() + index;
        }
    }

    void resize(size_t new_size, const T& value = T()) {
        if (new_size == this->element_count) {
            return;
        }

        if (new_size < this->element_count) {
            while (this->element_count > new_size) {
                this->pop_back();
            }
        } else {
            size_t diff = new_size - this->element_count;

            for (size_t i = 0; i < diff; ++i) {
                this->push_back(value);
            }
        }
    }

    void shrink_to_fit() {
        size_t used_blocks = this->end_block - this->start_block + 1;

        if (used_blocks < this->map_capacity / 4 && this->map_capacity > 8) {
            size_t new_capacity = std::max(8ul, used_blocks * 2);
            T** new_map         = new T*[new_capacity];
            memset(new_map, 0, new_capacity * sizeof(T*));

            size_t new_start_block = (new_capacity - used_blocks) / 2;

            memcpy(new_map + new_start_block, this->map + this->start_block,
                   used_blocks * sizeof(T*));

            delete[] this->map;

            this->map          = new_map;
            this->start_block  = new_start_block;
            this->end_block    = new_start_block + used_blocks - 1;
            this->map_capacity = new_capacity;
        }
    }

   private:
    void allocate_map(size_t capacity) {
        this->map = new T*[capacity];
        memset(this->map, 0, capacity * sizeof(T*));
        this->map_capacity = capacity;
    }

    void ensure_block(size_t map_index) {
        if (this->map[map_index] == nullptr) {
            this->map[map_index] = new T[BLOCK_SIZE];
        }
    }

    void resize_map() {
        size_t old_num_blocks = this->end_block - this->start_block + 1;
        size_t new_capacity   = std::max(8ul, this->map_capacity * 2);

        T** new_map = new T*[new_capacity];
        memset(new_map, 0, new_capacity * sizeof(T*));

        size_t new_start_block = (new_capacity - old_num_blocks) / 2;

        memcpy(new_map + new_start_block, this->map + this->start_block,
               old_num_blocks * sizeof(T*));

        delete[] this->map;
        this->map          = new_map;
        this->start_block  = new_start_block;
        this->end_block    = new_start_block + old_num_blocks - 1;
        this->map_capacity = new_capacity;
    }

    void chunked_move_backward(DequeIterator<T> first, DequeIterator<T> last,
                               DequeIterator<T> result) {
        if (first == last) {
            return;
        }

        if constexpr (std::is_trivially_copyable<T>::value) {
            while (last != first) {
                T* src_end   = last.cur;
                T* src_begin = last.first;

                T* dest_end   = result.cur;
                T* dest_begin = result.first;

                if (last.node == first.node) {
                    src_begin = first.cur;
                }

                size_t src_len  = src_end - src_begin;
                size_t dest_len = dest_end - dest_begin;
                size_t n        = std::min(src_len, dest_len);

                memmove(dest_end - n, src_end - n, n * sizeof(T));

                last -= n;
                result -= n;
            }
        } else {
            while (last != first) {
                --last;
                --result;
                *result = std::move(*last);
            }
        }
    }

    void chunked_move_forward(DequeIterator<T> first, DequeIterator<T> last,
                              DequeIterator<T> result) {
        if (first == last) {
            return;
        }

        if constexpr (std::is_trivially_copyable<T>::value) {
            while (first != last) {
                T* src_curr  = first.cur;
                T* src_limit = first.last;

                if (first.node == last.node) {
                    src_limit = last.cur;
                }

                T* dest_curr  = result.cur;
                T* dest_limit = result.last;

                size_t src_len  = src_limit - src_curr;
                size_t dest_len = dest_limit - dest_curr;
                size_t n        = std::min(src_len, dest_len);

                memmove(dest_curr, src_curr, n * sizeof(T));

                first += n;
                result += n;
            }
        } else {
            while (first != last) {
                *result = std::move(*first);
                ++first;
                ++result;
            }
        }
    }

    T** map;
    size_t map_capacity;
    size_t start_block;
    size_t start_offset;
    size_t end_block;
    size_t end_offset;
    size_t element_count;
};
}  // namespace kernel