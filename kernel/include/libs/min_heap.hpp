#pragma once

#include "libs/vector.hpp"
#include <algorithm>

namespace kernel {
template <typename T, int D = 4>
class MinHeap {
   public:
    using iterator        = typename Vector<T>::iterator;
    using const_iterator  = typename Vector<T>::const_iterator;
    using value_type      = T;
    using reference       = T&;
    using const_reference = const T&;

   public:
    MinHeap() = default;

    template <typename InputIterator>
    MinHeap(InputIterator first, InputIterator last) : m_data(first, last) {
        if (this->m_data.size() > 1) {
            // Start sifting down from the last non-leaf node
            for (int i = (this->m_data.size() - 2) / D; i >= 0; --i) {
                this->sift_down(static_cast<size_t>(i));
            }
        }
    }

    void insert(const T& value) {
        this->m_data.push_back(value);
        this->sift_up(this->m_data.size() - 1);
    }

    void insert(T&& value) {
        this->m_data.push_back(std::move(value));
        this->sift_up(this->m_data.size() - 1);
    }

    template <typename... Args>
    void emplace(Args&&... args) {
        this->m_data.emplace_back(std::forward<Args>(args)...);
        this->sift_up(this->m_data.size() - 1);
    }

    bool extract_min(T& out_value) {
        if (this->m_data.empty()) {
            LOG_ERROR("Heap Underflow");
            return false;
        }

        out_value = std::move(this->m_data[0]);

        // Move last element to root
        T last = std::move(this->m_data.back());
        this->m_data.pop_back();

        if (!this->m_data.empty()) {
            this->m_data[0] = std::move(last);
            this->sift_down(0);
        }

        return true;
    }

    iterator erase(const_iterator pos) {
        size_t index = std::distance(this->m_data.cbegin(), pos);

        if (index >= this->m_data.size()) {
            return this->m_data.end();
        }

        // 1. Move the last element to the erased position
        T last = std::move(this->m_data.back());
        this->m_data.pop_back();

        if (index == this->m_data.size()) {
            return this->m_data.end();
        }

        // 2. Restore heap property
        // The element we moved to 'index' might be smaller than parent OR larger than children

        // Place item temporarily
        this->m_data[index] = std::move(last);

        size_t parent_idx = (index - 1) / D;

        // Check Up condition
        if (index > 0 && this->less(this->m_data[index], this->m_data[parent_idx])) {
            this->sift_up(index);
        } else {
            // Check Down condition
            this->sift_down(index);
        }

        // Return valid iterator (points to the element that now occupies this slot)
        return this->m_data.begin() + index;
    }

    template <typename Predicate>
    size_t erase_if(Predicate pred) {
        // Partition the vector: Move items to keep to the front
        auto old_end = this->m_data.end();
        auto new_end = std::remove_if(this->m_data.begin(), this->m_data.end(), pred);

        size_t count = std::distance(new_end, old_end);

        if (count > 0) {
            // Chop off the removed elements
            this->m_data.erase(new_end, old_end);

            // Rebuild heap
            if (this->m_data.size() > 1) {
                for (int i = (this->m_data.size() - 2) / D; i >= 0; --i) {
                    this->sift_down(static_cast<size_t>(i));
                }
            }
        }
        return count;
    }

    const T& top() const {
        return this->m_data.front();
    }

    bool empty() const {
        return this->m_data.empty();
    }

    size_t size() const {
        return this->m_data.size();
    }

    void reserve(size_t n) {
        this->m_data.reserve(n);
    }

    void clear() {
        this->m_data.clear();
    }

    iterator begin() {
        return this->m_data.begin();
    }

    iterator end() {
        return this->m_data.end();
    }

    const_iterator begin() const {
        return this->m_data.begin();
    }

    const_iterator end() const {
        return this->m_data.end();
    }

    const_iterator cbegin() const {
        return this->m_data.cbegin();
    }

    const_iterator cend() const {
        return this->m_data.cend();
    }

   private:
    bool less(const T& a, const T& b) const {
        return a < b;
    }

    void sift_up(size_t index) {
        if (index == 0) {
            return;
        }

        // Save the target being moved up
        T target = std::move(this->m_data[index]);

        while (index > 0) {
            size_t parent_idx = (index - 1) / D;

            // If target < parent, move parent down into the hole
            if (this->less(target, this->m_data[parent_idx])) {
                this->m_data[index] = std::move(this->m_data[parent_idx]);
                index               = parent_idx;  // Move hole up
            } else {
                break;
            }
        }

        // Place target into the final hole
        this->m_data[index] = std::move(target);
    }

    void sift_down(size_t index) {
        size_t size = this->m_data.size();

        if (index >= size) {
            return;
        }

        T target = std::move(this->m_data[index]);

        while (true) {
            size_t child_start = (D * index) + 1;

            if (child_start >= size) {
                break;
            }

            // Find smallest child among D children
            size_t smallest_child = child_start;
            size_t child_end      = child_start + D;
            if (child_end > size) child_end = size;

            for (size_t i = child_start + 1; i < child_end; ++i) {
                if (this->less(this->m_data[i], this->m_data[smallest_child])) {
                    smallest_child = i;
                }
            }

            // If target > smallest child, move child up into the hole
            if (this->less(this->m_data[smallest_child], target)) {
                this->m_data[index] = std::move(this->m_data[smallest_child]);
                index               = smallest_child;  // Move hole down
            } else {
                break;
            }
        }

        // Place target into the final hole
        this->m_data[index] = std::move(target);
    }

    Vector<T> m_data;
};
}  // namespace kernel