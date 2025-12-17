#include "memory/pcid_manager.hpp"
#include "memory/paging.hpp"

namespace kernel::memory {
void PcidManager::flush_hardware_pcid(uint16_t pcid) {
    TLB::flush_context(pcid);
}
}  // namespace kernel::memory