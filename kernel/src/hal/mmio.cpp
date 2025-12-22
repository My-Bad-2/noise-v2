#include "hal/mmio.hpp"
#include "memory/memory.hpp"
#include "memory/vmm.hpp"
#include "libs/log.hpp"

namespace kernel::hal {
using namespace memory;

MMIORegion::MMIORegion(uintptr_t phys_addr, size_t size, memory::CacheType cache) : size(size) {
    uintptr_t offset          = phys_addr % PAGE_SIZE_4K;
    uintptr_t phys_page_start = phys_addr - offset;

    size_t total_size = size + offset;
    size_t num_pages  = (total_size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;

    this->mapped_size = num_pages * PAGE_SIZE_4K;

    // Reserve a contiguous virtual window for the MMIO range.
    this->page_base = VirtualManager::reserve_mmio(this->mapped_size, PAGE_SIZE_4K);

    if (!this->page_base) {
        LOG_ERROR("MMIO: failed to reserve virtual space for phys=0x%lx size=0x%zx", phys_addr,
                  size);
        return;
    }

    // Map each 4K page with the requested cache policy into the reserved region.
    for (size_t i = 0; i < this->mapped_size; i += PAGE_SIZE_4K) {
        if (!VirtualManager::curr_map()->map(reinterpret_cast<uintptr_t>(this->page_base) + i,
                                             phys_page_start + i, Read | Write, cache,
                                             PageSize::Size4K)) {
            LOG_ERROR("MMIO: failed to map page for phys=0x%lx", phys_page_start + i);
            return;
        }
    }

    this->virt_base = reinterpret_cast<uintptr_t>(this->page_base) + offset;
    LOG_INFO("MMIO: mapped phys=0x%lx size=0x%zx at virt=0x%lx", phys_addr, size, this->virt_base);
}

// MMIORegion::~MMIORegion() {
    // VirtualManager::free(this->page_base, false);
// }

volatile void* MMIORegion::ptr() const {
    return reinterpret_cast<volatile void*>(this->virt_base);
}
}  // namespace kernel::hal