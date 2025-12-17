#pragma once

#include "task/process.hpp"

#ifdef __x86_64__
#define MAX_PCID_NUM 4096
#else
// AArch64's ASID supports 2^16 address space identifiers
#define MAX_PCID_NUM 65536
#endif

namespace kernel::memory {
class PcidManager {
   public:
    void init();

    uint16_t get_pcid(task::Process* proc);
    void free_pcid(uint16_t pcid);

    static PcidManager& get();

   private:
    uint16_t allocate_new(task::Process* proc, size_t cpuid);
    void claim_slot(uint16_t pcid, task::Process* proc, size_t cpuid);
    void flush_hardware_pcid(uint16_t pcid);

    task::Process* slots[MAX_PCID_NUM];
    uint64_t used_bitmap[MAX_PCID_NUM / 64];
    uint16_t victim_iterator = 1;
};
}  // namespace kernel::memory