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