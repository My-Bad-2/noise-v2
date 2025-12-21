#include "boot/boot.h"
#include "memory/memory.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "memory/heap.hpp"

namespace kernel::memory {
namespace __details {
uintptr_t hhdm_offset = 0;
}

void init() {
    __details::hhdm_offset = hhdm_request.response->offset;

    PhysicalManager::init();
    VirtualManager::init();
    SlubAllocator::get().init();
}
}  // namespace kernel::memory