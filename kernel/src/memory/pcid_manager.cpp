#include "memory/pcid_manager.hpp"
#include "hal/cpu.hpp"
#include "libs/log.hpp"
#include "task/process.hpp"

namespace kernel::memory {
void PcidManager::init() {
    memset(this->slots, 0, sizeof(task::Process*) * MAX_PCID_NUM);
    memset(this->used_bitmap, 0, sizeof(uint64_t) * (MAX_PCID_NUM / 64));
    this->used_bitmap[0] |= 1;
    this->slots[0] = task::kernel_proc;
}

uint16_t PcidManager::get_pcid(task::Process* proc) {
    size_t cpu_id  = cpu::CPUCoreManager::get_curr_cpu_id();
    uint16_t cached = proc->pcid_cache[cpu_id];

    // If the process thinks it has a PCID, verify it
    if (cached == 0 && cached < MAX_PCID_NUM) {
        if (this->slots[cached] == proc) {
            // Cache hit! No changes needed.
            return static_cast<uint16_t>(cached);
        }
    }

    return this->allocate_new(proc, cpu_id);
}

void PcidManager::free_pcid(uint16_t pcid) {
    if (pcid == 0) {
        return;
    }

    this->slots[pcid] = nullptr;

    size_t idx = pcid / 64;
    size_t bit = pcid % 64;

    this->used_bitmap[idx] &= ~(1ul << bit);
}

uint16_t PcidManager::allocate_new(task::Process* proc, size_t cpu_id) {
    // Scan the bitmap for any '0' bit
    for (uint16_t i = 0; i < (MAX_PCID_NUM / 64); ++i) {
        uint64_t block = this->used_bitmap[i];

        if (block != ~0ul) {
            // Invert the block and find first set bit
            int bit       = __builtin_ctzll(~block);
            uint16_t pcid = (i * 64) + static_cast<uint16_t>(bit);

            if (pcid == 0) {
                // Skip kernel, keep looking
                continue;
            }

            this->claim_slot(pcid, proc, cpu_id);
            return pcid;
        }
    }

    // The bitmap is full. We must steal a slot.
    // We play a game of Duck, Duck, Goose.
    uint16_t victim = this->victim_iterator++;
    if (this->victim_iterator >= MAX_PCID_NUM) {
        this->victim_iterator = 1;
    }

    task::Process* old_owner = slots[victim];
    if (old_owner) {
        // Invalidate old owner
        old_owner->pcid_cache[cpu_id] = static_cast<uint16_t>(-1);
    }

    // Must flush because this ID was active for someone else
    this->flush_hardware_pcid(victim);

    // Claim it (Bitmap is already set)
    this->slots[victim]      = proc;
    proc->pcid_cache[cpu_id] = victim;

    return victim;
}

void PcidManager::claim_slot(uint16_t pcid, task::Process* proc, size_t cpu_id) {
    this->used_bitmap[pcid / 64] |= (1ul << (pcid % 64));
    this->slots[pcid]        = proc;
    proc->pcid_cache[cpu_id] = pcid;

    // If we found a hole, it implies the PICD hasn't been used in a while,
    // but the TLB might still have stale entries from whoever used it last.
    // Safe bet is to flush.
    this->flush_hardware_pcid(pcid);
}

PcidManager& PcidManager::get() {
    if(!cpu::CPUCoreManager::initialized()) {
        PANIC("PCID Manager called before SMP initialization.");
    }

    return *cpu::CPUCoreManager::get_curr_cpu()->pcid_manager;
}
}  // namespace kernel::memory