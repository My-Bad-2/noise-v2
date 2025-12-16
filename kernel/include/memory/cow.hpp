#pragma once

#include <cstdint>
#include "memory/pagemap.hpp"

namespace kernel::memory {
class CowManager {
   public:
    static void init();
    static uintptr_t get_zero_page_phys();
    static bool is_zero_page(uintptr_t virt_addr, PageMap* map);
    static bool handle_fault(uintptr_t virt_addr, PageMap* map);

    static bool initialized() {
        return zero_page_phys == 0;
    }

   private:
    static uintptr_t zero_page_phys;
};
}  // namespace kernel::memory