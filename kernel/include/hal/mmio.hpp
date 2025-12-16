#pragma once

#include "memory/pagemap.hpp"

namespace kernel::hal {

class MMIORegion {
   public:
    MMIORegion() : virt_base(0), size(0) {}

    MMIORegion(uintptr_t phys_addr, size_t size, memory::CacheType cache = memory::CacheType::Uncached);

    template <typename T>
    void write(size_t offset, T value) {
        volatile T* addr = reinterpret_cast<volatile T*>(this->virt_base + offset);
        *addr            = value;
    }

    template <typename T>
    T read(size_t offset) {
        volatile T* addr = reinterpret_cast<volatile T*>(this->virt_base + offset);
        return *addr;
    }

    template <typename T>
    void write_at(size_t index, T val) {
        write<T>(index * sizeof(T), val);
    }

    template <typename T>
    T read_at(size_t index) {
        return read<T>(index * sizeof(T));
    }

    volatile void* ptr() const;

   private:
    uintptr_t virt_base;
    size_t size;
    size_t mapped_size;
    void* page_base;
};
}  // namespace kernel::hal