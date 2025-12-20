#pragma once

#include <iterator>

namespace kernel {
struct DefaultTag {};

template <typename Tag = DefaultTag>
struct IntrusiveListNode {
    IntrusiveListNode* prev = this;
    IntrusiveListNode* next = this;

    [[gnu::always_inline]] inline void unlink() {
        IntrusiveListNode* p = prev;
        IntrusiveListNode* n = next;
        n->prev              = p;
        p->next              = n;
    }

    [[gnu::always_inline]] inline bool is_linked() const {
        return next != this;
    }
};

template <typename Tag, typename T>
bool is_linked(const T& obj) {
    return static_cast<const IntrusiveListNode<Tag>&>(obj).is_linked();
}

template <typename Tag, typename T>
bool is_linked(const T* obj) {
    if (!obj) {
        return false;
    }

    return static_cast<const IntrusiveListNode<Tag>&>(obj)->is_linked();
}

template <typename T, typename Tag = DefaultTag, bool AutoUnlink = false>
class IntrusiveList {
    using Node = IntrusiveListNode<Tag>;

   public:
    class Iterator {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = T*;
        using reference         = T&;

        [[gnu::always_inline]] inline Iterator(Node* node_) : node_(node_) {}

        [[gnu::always_inline]] inline reference operator*() const {
            return *static_cast<T*>(this->node_);
        }

        [[gnu::always_inline]] inline pointer operator->() const {
            return static_cast<T*>(this->node_);
        }

        [[gnu::always_inline]] inline Iterator& operator++() {
            this->node_ = this->node_->next;
            __builtin_prefetch(this->node_->next);
            return *this;
        }

        [[gnu::always_inline]] inline Iterator operator++(int) {
            Iterator tmp = *this;
            this->node_  = this->node_->next;
            __builtin_prefetch(this->node_->next);
            return tmp;
        }

        [[gnu::always_inline]] inline Iterator& operator--() {
            this->node_ = this->node_->prev;
            return *this;
        }

        [[gnu::always_inline]] inline Iterator operator--(int) {
            Iterator tmp = *this;
            this->node_  = this->node_->prev;
            return tmp;
        }

        [[gnu::always_inline]] inline bool operator==(const Iterator& other) const {
            return this->node_ == other.node_;
        }

        [[gnu::always_inline]] inline bool operator!=(const Iterator& other) const {
            return this->node_ != other.node_;
        }

        [[gnu::always_inline]] inline Node* node() const {
            return this->node_;
        }

       private:
        Node* node_;
    };

    IntrusiveList() noexcept {
        this->root.next = &this->root;
        this->root.prev = &this->root;
    }

    IntrusiveList(IntrusiveList&& other) noexcept {
        if (other.empty()) {
            this->root.next = &this->root;
            this->root.prev = &this->root;
        } else {
            // Steal links
            this->root.next = other.root.next;
            this->root.prev = other.root.prev;

            this->root.next->prev = &this->root;
            this->root.prev->next = &this->root;

            // Reset other
            other.root.next = &other.root;
            other.root.prev = &other.root;
        }
    }

    [[gnu::always_inline]] inline bool empty() const noexcept {
        return this->root.next == &this->root;
    }

    [[gnu::always_inline]] inline Iterator begin() noexcept {
        return Iterator(this->root.next);
    }

    [[gnu::always_inline]] inline Iterator end() noexcept {
        return Iterator(&this->root);
    }

    [[gnu::always_inline]] inline T& front() noexcept {
        return *static_cast<T*>(this->root.next);
    }

    [[gnu::always_inline]] inline T& back() noexcept {
        return *static_cast<T*>(this->root.prev);
    }

    [[gnu::always_inline]] inline void push_back(T& value) noexcept {
        Node* n    = static_cast<Node*>(&value);
        Node* prev = this->root.prev;

        n->next         = &this->root;
        n->prev         = prev;
        prev->next      = n;
        this->root.prev = n;
    }

    [[gnu::always_inline]] inline void push_front(T& value) noexcept {
        Node* n    = static_cast<Node*>(&value);
        Node* next = this->root.next;

        n->prev         = &this->root;
        n->next         = next;
        next->prev      = n;
        this->root.next = n;
    }

    [[gnu::always_inline]] inline void emplace_back(T& value) noexcept {
        push_back(value);
    }

    [[gnu::always_inline]] inline void emplace_front(T& value) noexcept {
        push_front(value);
    }

    [[gnu::always_inline]] inline Iterator insert(Iterator pos, T& value) noexcept {
        Node* n    = static_cast<Node*>(&value);
        Node* next = pos.node();
        Node* prev = next->prev;

        n->next    = next;
        n->prev    = prev;
        prev->next = n;
        next->prev = n;

        return Iterator(n);
    }

    [[gnu::always_inline]] inline Iterator erase(Iterator pos) noexcept {
        Node* n    = pos.node();
        Node* next = n->next;
        Node* prev = n->prev;

        prev->next = next;
        next->prev = prev;

        return Iterator(next);
    }

    [[gnu::always_inline]] inline void pop_front() noexcept {
        Node* n         = this->root.next;
        Node* next      = n->next;
        this->root.next = next;
        next->prev      = &this->root;
    }

    [[gnu::always_inline]] inline void pop_back() noexcept {
        Node* n         = this->root.prev;
        Node* prev      = n->prev;
        this->root.prev = prev;
        prev->next      = &this->root;
    }

    void clear() noexcept {
        if constexpr (AutoUnlink) {
            Node* cur = this->root.next;
            while (cur != &this->root) {
                Node* next = cur->next;
                cur->prev = cur->next = cur;  // Reset node
                cur                   = next;
            }
        }

        this->root.next = &this->root;
        this->root.prev = &this->root;
    }

    [[gnu::always_inline]] inline void remove(T& value) noexcept {
        Node* n = static_cast<Node*>(&value);

        // If the node is not linked, do nothing
        if (n->next == n) {
            return;
        }

        Node* next = n->next;
        Node* prev = n->prev;

        prev->next = next;
        next->prev = prev;

        n->next = n;
        n->prev = n;
    }

    [[gnu::always_inline]] inline void remove(T* value) noexcept {
        if (value) {
            this->remove(*value);
        }
    }

    friend class Iterator;

   private:
    Node root;
};
}  // namespace kernel