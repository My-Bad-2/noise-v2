#include <new>
#include "memory/heap.hpp"

using namespace kernel::memory;

void* operator new(std::size_t size) {
    return kmalloc(size);
}

void* operator new[](std::size_t size) {
    return kmalloc(size);
}

void operator delete(void* ptr) {
    kfree(ptr);
}

void operator delete[](void* ptr) {
    kfree(ptr);
}

void operator delete(void* ptr, std::size_t) {
    kfree(ptr);
}

void operator delete[](void* ptr, std::size_t) {
    kfree(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return kmalloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return kmalloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    kfree(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    kfree(ptr);
}

void* operator new(std::size_t size, std::align_val_t align) {
    return aligned_kalloc(size, static_cast<size_t>(align));
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return aligned_kalloc(size, static_cast<size_t>(align));
}

void* operator new[](std::size_t size, std::align_val_t align) {
    return aligned_kalloc(size, static_cast<size_t>(align));
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return aligned_kalloc(size, static_cast<size_t>(align));
}

void operator delete[](void* ptr, std::align_val_t) {
    aligned_kfree(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    aligned_kfree(ptr);
}
