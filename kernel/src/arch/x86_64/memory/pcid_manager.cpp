#include "memory/pcid_manager.hpp"
#include <cstdint>
#include "hal/cpu.hpp"
#include "memory/paging.hpp"

namespace kernel::memory {
void PcidManager::flush_hardware_pcid(uint16_t pcid) {
    TLB::flush_context(pcid);
}

void PcidManager::force_invalidate(uint16_t pcid) {
    if (pcid == 0 || pcid >= MAX_PCID_NUM) {
        return;
    }

    task::Process* owner = slots[pcid];

    if (owner) {
        // Tell the process it lost its badge
        uint32_t cpu           = cpu::CPUCoreManager::get_curr_cpu_id();
        owner->pcid_cache[cpu] = static_cast<uint16_t>(-1);

        // Clear our record
        // We don't need to clear the bitmap but we must ensure that
        // whoever gets this slot next flushes it.
        this->free_pcid(pcid);
    }
}
}  // namespace kernel::memory