/**
 * @file memory.cpp
 * @brief Memory subsystem initialization glue.
 *
 * Reads the higher-half direct-map offset from the bootloader and
 * initializes the physical memory manager.
 */

#include "boot/boot.h"
#include "memory/memory.hpp"
#include "memory/pmm.hpp"
#include "libs/log.hpp"
#include "memory/vmm.hpp"
#include "memory/heap.hpp"

namespace kernel::memory {
namespace __details {
uintptr_t hhdm_offset = 0;
}

void init() {
    // HHDM offset provided by Limine.
    __details::hhdm_offset = hhdm_request.response->offset;
    LOG_INFO("Memory HHDM offset set to 0x%lx", __details::hhdm_offset);

    // Initialize the physical memory manager.
    PhysicalManager::init();

    // Initialize the Virtual memory manager.
    VirtualManager::init();
    
    KernelHeap& kheap = KernelHeap::instance();
    kheap.init();
}

}  // namespace kernel::memory