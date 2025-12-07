#include "memory/pagemap.hpp"
#include "memory/memory.hpp"
#include "memory/paging.hpp"
#include "memory/pmm.hpp"
#include "cpu/registers.hpp"
#include "cpu/regs.h"
#include "cpu/features.hpp"
#include "libs/log.hpp"
#include "libs/math.hpp"

// PAT Memory Types
#define PAT_TYPE_UC  0x00ULL  // Uncacheable
#define PAT_TYPE_WC  0x01ULL  // Write-Combining
#define PAT_TYPE_WT  0x04ULL  // Write-Through
#define PAT_TYPE_WP  0x05ULL  // Write-Protect
#define PAT_TYPE_WB  0x06ULL  // Write-Back
#define PAT_TYPE_UC_ 0x07ULL  // Uncached (Weak)

// NOLINTBEGIN(performance-no-int-to-ptr)
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
namespace kernel::memory {
bool TLB::has_invpcid = false;
namespace {
int max_levels        = 4;
bool support_1g_pages = false;
bool support_nx       = false;
bool pcid_supported   = false;

// Map abstract PageSize to page-table tree depth. This isolates the
// rest of the code from hard-wiring architectural levels.
int get_target_level(PageSize size) {
    switch (size) {
        case PageSize::Size4K:
            return 1;
        case PageSize::Size2M:
            return 2;
        case PageSize::Size1G:
            return 3;
        default:
            return 0;
    }
}

// Translate high-level flags/cache policy into raw PTE bits.
size_t convert_generic_flags(uint8_t flags, CacheType cache, PageSize size) {
    size_t ret       = 0;
    const size_t pat = (size == PageSize::Size4K) ? FlagPAT : FlagLPAT;

    if (flags & Read) {
        ret |= FlagPresent;
    }

    if (flags & Write) {
        ret |= FlagWrite;
    }

    if (flags & User) {
        ret |= FlagUser;
    }

    if (flags & Global) {
        ret |= FlagGlobal;
    }

    if (!(flags & Execute) && support_nx) {
        ret |= FlagNoExec;
    }

    if (flags & Lazy) {
        ret |= FlagLazy;
    }

    if (size != PageSize::Size4K) {
        ret |= FlagHuge;
    }

    switch (cache) {
        case CacheType::Uncached:
            ret |= FlagCacheDisable | FlagWriteThrough;
            break;
        case CacheType::WriteProtected:
            ret |= pat;
            break;
        case CacheType::WriteThrough:
            ret |= FlagWriteThrough;
            break;
        case CacheType::WriteCombining:
            ret |= pat | FlagWriteThrough;
            break;
        case CacheType::WriteBack:
            break;
    }

    return ret;
}

std::pair<uint8_t, CacheType> convert_arch_flags(size_t flags, PageSize size) {
    uint8_t flag = Execute;
    {
        if (flags & FlagPresent) {
            flag |= Read;
        }

        if (flags & FlagWrite) {
            flag |= Write;
        }

        if (flags & FlagUser) {
            flag |= User;
        }

        if (flags & FlagGlobal) {
            flag |= Global;
        }

        if (flags & FlagNoExec) {
            flag &= ~Execute;
        }
    }

    CacheType cache = CacheType::WriteBack;
    {
        bool is_pat = (size == PageSize::Size4K) ? (flags & FlagPAT) : (flags & FlagLPAT);
        bool is_pcd = (flags & FlagCacheDisable);
        bool is_pwt = (flags & FlagWriteThrough);

        if (!is_pat && !is_pcd && !is_pwt) {
            cache = CacheType::WriteBack;
        } else if (!is_pat && !is_pcd && is_pwt) {
            cache = CacheType::WriteThrough;
        } else if (!is_pat && is_pcd && is_pwt) {
            cache = CacheType::Uncached;
        } else if (is_pat && !is_pcd && is_pwt) {
            cache = CacheType::WriteCombining;
        } else if (is_pat && !is_pcd && !is_pwt) {
            cache = CacheType::WriteProtected;
        }
    }

    return std::make_pair(flag, cache);
}

void init_pat() {
    // Program the PAT with a canonical layout (UC/WC/WT/WB, etc.). This
    // makes later cache attribute selections uniform across the kernel.
    arch::Msr pat = arch::Msr::read(MSR_PAT);

    pat.value |= PAT_TYPE_WB << 0;
    pat.value |= PAT_TYPE_WT << 8;
    pat.value |= PAT_TYPE_UC_ << 16;
    pat.value |= PAT_TYPE_UC << 24;
    pat.value |= PAT_TYPE_WC << 32;
    pat.value |= PAT_TYPE_WT << 40;
    pat.value |= PAT_TYPE_UC_ << 48;
    pat.value |= PAT_TYPE_UC << 56;

    pat.write();
    LOG_INFO("Paging: PAT initialized");
}
}  // namespace

void TLB::flush(uintptr_t virt_addr) {
    // Single-page invalidation: cheap and precise when editing PTEs.
    asm volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");
}

void TLB::flush_hard() {
    arch::Cr4 cr4 = arch::Cr4::read();

    // Toggling PGE forces a broad TLB flush on hardware that lacks INVPCID.
    cr4.pge ^= 1;
    cr4.write();

    // Return to the original state.
    cr4.pge ^= 1;
    cr4.write();
}

void TLB::flush_specific(uintptr_t virt_addr, uint16_t pcid) {
    if (has_invpcid) {
        arch::InvpcidDesc desc = {};
        desc.pcid              = pcid;
        desc.addr              = virt_addr;
        desc.rsvd              = 0;
        desc.flush(arch::InvpcidType::IndivdualAddress);
    } else {
        // for Non-INVPCID systems:
        // We cannot selectively flush another context without switching to it.
        // This is handled by the Scheduler later invalidating
        // that process's PCID on next switch, or by sending an IPI.
        // For now, force a full flush.
        flush_hard();
    }
}

void TLB::flush_context(uint16_t pcid) {
    if (has_invpcid) {
        arch::InvpcidDesc desc = {};
        desc.pcid              = pcid;
        desc.flush(arch::InvpcidType::SingleContext);
    } else {
        // Fallback
        arch::Cr3 cr3                = arch::Cr3::read();
        uint16_t current_active_pcid = cr3.pcid_enabled.pcid;

        if (pcid == current_active_pcid) {
            // Flush the current context.
            cr3.write();
        } else {
            // We need to flush an inactive pcid, since we
            // lack the hardware instruction and a scheduler
            // to defer it. We must nuke the entire TLB to
            // ensure that the target PCID is clean.
            flush_hard();
        }
    }
}

void TLB::flush_all() {
    if (has_invpcid) {
        arch::InvpcidDesc desc = {};
        desc.flush(arch::InvpcidType::AllContextsRetainGlobals);
    } else {
        // Reload CR3 on legacy systems
        arch::Cr3 cr3 = arch::Cr3::read();
        cr3.write();
    }
}

// NOLINTNEXTLINE
uintptr_t* PageMap::get_pte(uintptr_t virt_addr, int target_level, bool allocate) {
    uintptr_t curr_table_phys = this->phys_root_addr;

    for (int level = max_levels; level > target_level; --level) {
        uintptr_t* table_virt = reinterpret_cast<uintptr_t*>(to_higher_half(curr_table_phys));

        int shift = 12 + (level - 1) * 9;
        int index = static_cast<int>((virt_addr >> shift) & 0x1FF);

        uintptr_t entry = table_virt[index];

        if (entry & FlagHuge) {
            // Refuse to split an existing huge-page mapping implicitly;
            // callers must explicitly tear it down if they want finer granularity.
            return nullptr;
        }

        if (!(entry & FlagPresent)) {
            if (!allocate) {
                return nullptr;
            }

            // Lazily allocate the next page-table level only when needed;
            // this keeps paging structures sparse and reduces memory usage.
            uintptr_t new_table_phys = reinterpret_cast<uintptr_t>(PhysicalManager::alloc());
            if (new_table_phys == 0) {
                LOG_ERROR("PageMap: failed to allocate page table at level=%d", level);
                return nullptr;
            }

            uintptr_t* new_table_virt =
                reinterpret_cast<uintptr_t*>(to_higher_half(new_table_phys));

            for (int i = 0; i < 512; ++i) {
                new_table_virt[i] = 0;
            }

            uint64_t new_entry = new_table_phys | FlagPresent | FlagWrite | FlagUser;
            table_virt[index]  = new_entry;
            entry              = new_entry;
        }

        curr_table_phys = entry & page_mask;
    }

    uintptr_t* target_table_virt = reinterpret_cast<uintptr_t*>(to_higher_half(curr_table_phys));
    int shift                    = 12 + (target_level - 1) * 9;
    int target_idx               = static_cast<int>((virt_addr >> shift) & 0x1FF);

    return &target_table_virt[target_idx];
}

bool PageMap::map(uintptr_t virt_addr, uintptr_t phys_addr, uint8_t flags, CacheType cache,
                  PageSize size, uint8_t pkey, bool do_flush) {
    int target_level = get_target_level(size);
    size_t mask      = 0;  // Alignment mask

    const size_t arch_flags = convert_generic_flags(flags, cache, size);

    if (size == PageSize::Size2M) {
        mask = PAGE_SIZE_2M;
    } else if (size == PageSize::Size1G) {
        mask = PAGE_SIZE_1G;

        if (!support_1g_pages) {
            return false;
        }
    } else {
        mask = PAGE_SIZE_4K;
    }

    if (!is_aligned(virt_addr, mask) || !is_aligned(phys_addr, mask)) {
        return false;
    }

    uintptr_t* pte = get_pte(virt_addr, target_level, true);
    if (!pte) {
        return false;
    }

    uint64_t entry = phys_addr & page_mask;
    entry |= arch_flags;

    // Apply PKEY (Bits 59-62)
    entry |= (static_cast<uint64_t>(pkey & 0xF) << 59);
    *pte = entry;

    if (this->is_active()) {
        if (do_flush) {
            TLB::flush(virt_addr);
        }
    } else {
        this->is_dirty = true;
    }

    return true;
}

bool PageMap::map(uintptr_t virt_addr, uint8_t flags, CacheType cache, PageSize size,
                  bool do_flush) {
    // This overload is responsible for owning new physical frames and
    // then delegating to the core mapping function. On failure it eagerly
    // frees the frame to avoid leaks.
    size_t frames_needed = 0;
    uintptr_t phys_addr  = 0;

    switch (size) {
        case PageSize::Size4K:
            phys_addr     = reinterpret_cast<uintptr_t>(PhysicalManager::alloc());
            frames_needed = 1;
            break;

        case PageSize::Size2M:
            // 2 MiB = 512 * 4 KiB pages
            // Must be aligned to 2 MiB (0x200000)
            frames_needed = 512ull;
            phys_addr     = reinterpret_cast<uintptr_t>(
                PhysicalManager::alloc_aligned(frames_needed, PAGE_SIZE_2M));
            break;

        case PageSize::Size1G:
            // 1 GiB = 512 * 512 * 4 KiB pages
            // Must be aligned to 1 GiB (0x40000000)
            if (!support_1g_pages) {
                return false;
            }

            frames_needed = 512ull * 512ull;
            phys_addr     = reinterpret_cast<uintptr_t>(
                PhysicalManager::alloc_aligned(frames_needed, PAGE_SIZE_1G));
            break;
    }

    if (phys_addr == 0) {
        return false;
    }

    if (!this->map(virt_addr, phys_addr, flags, cache, size, 0, do_flush)) {
        PhysicalManager::free(reinterpret_cast<void*>(phys_addr), frames_needed);
        return false;
    }

    return true;
}

void PageMap::unmap(uintptr_t virt_addr, uint16_t owner_pcid, bool free_phys) {
    uint64_t curr_tbl_phys = this->phys_root_addr;
    int level              = max_levels;

    while (level >= 1) {
        uintptr_t* table_virt = reinterpret_cast<uintptr_t*>(to_higher_half(curr_tbl_phys));

        int shift = 12 + (level - 1) * 9;
        int index = static_cast<int>((virt_addr >> shift) & 0x1FF);

        uint64_t entry = table_virt[index];

        // If not present, nothing to unmap
        if (!(entry & FlagPresent)) {
            return;
        }

        bool is_huge = (level > 1) && (entry & FlagHuge);
        bool is_leaf = (level == 1);

        if (is_huge || is_leaf) {
            uintptr_t phys_addr = entry & page_mask;

            // Remove the mapping before flushing
            table_virt[index] = 0;

            uint64_t curr_cr3_phys = arch::Cr3::read().raw & ~0xFFFull;
            uint64_t root_phys     = this->phys_root_addr & ~0xFFFull;

            if (curr_cr3_phys == root_phys) {
                // We're modifying the current address space.
                TLB::flush(virt_addr);
            } else {
                // Modifying inactive address space.
                // Target the specific PCID owner.
                TLB::flush_specific(virt_addr, owner_pcid);
            }

            if (free_phys) {
                if (level == 1) {
                    PhysicalManager::free(reinterpret_cast<void*>(phys_addr));
                } else if (level == 2) {
                    PhysicalManager::free(reinterpret_cast<void*>(phys_addr), 512);
                } else if (level == 3) {
                    PhysicalManager::free(reinterpret_cast<void*>(phys_addr), 512ull * 512ull);
                }
            }

            return;
        }

        curr_tbl_phys = entry & page_mask;
        --level;
    }
}

uintptr_t PageMap::translate(uintptr_t virt_addr) {
    // Walk the page tables similarly to hardware to reconstruct the
    // physical address; used mainly for debugging or low-level I/O.
    uintptr_t curr_table_phys = this->phys_root_addr;

    for (int level = max_levels; level >= 1; --level) {
        uintptr_t* table_virt = reinterpret_cast<uintptr_t*>(to_higher_half(curr_table_phys));

        // Calculate the index for this level
        // level 5: Bits 48-56
        // level 4: Bits 39-47
        // level 3: Bits 30-38
        // level 2: Bits 21-29
        // level 1: Bits 12-20
        int shift = 12 + (level - 1) * 9;
        int index = static_cast<int>((virt_addr >> shift) & 0x1FF);

        uint64_t entry = table_virt[index];

        if (level > 1 && (entry & FlagHuge)) {
            uint64_t offset_mask = (1ull << shift) - 1;
            uint64_t offset      = virt_addr & offset_mask;

            // Get pure physical address
            uint64_t phys_base = entry & page_mask;

            // For huge pages, the CPU ignores the lower bits of the physical
            // address that corresponds to the offset.
            // It's safer to mask the base ensuring it is aligned to the huge page
            // size.
            return (phys_base & ~offset_mask) + offset;
        }

        // Leaf Node
        if (level == 1) {
            uint64_t offset    = virt_addr & 0xFFF;
            uint64_t phys_base = entry & page_mask;

            return phys_base + offset;
        }

        curr_table_phys = entry & page_mask;
    }

    return 0;
}

void PageMap::create_new(PageMap* map) {
    static bool kernel_initialized = false;
    uintptr_t* root_phys           = static_cast<uintptr_t*>(PhysicalManager::alloc());

    if (!root_phys) {
        LOG_ERROR("PageMap::create_new: failed to allocate root table");
        return;
    }

    uint64_t* root_virt = to_higher_half(root_phys);
    for (int i = 0; i < 512; ++i) {
        root_virt[i] = 0;
    }

    // After the first initialization, new address spaces inherit the kernel
    // half of the address space by copying upper-level entries.
    if (kernel_initialized) {
        uintptr_t kmap       = get_kernel_map()->phys_root_addr;
        uintptr_t* kmap_virt = reinterpret_cast<uintptr_t*>(to_higher_half(kmap));

        for (int i = 256; i < 512; ++i) {
            root_virt[i] = kmap_virt[i];
        }
    }

    if (!kernel_initialized) {
        kernel_initialized = true;
    }

    map->phys_root_addr = reinterpret_cast<uintptr_t>(root_phys);
    map->is_dirty       = true;
    LOG_DEBUG("PageMap::create_new root_phys=0x%lx kernel_initialized=%d", map->phys_root_addr,
              kernel_initialized);
}

void PageMap::load(uint16_t pcid) {
    arch::Cr3 cr3 = {};
    cr3.raw       = this->phys_root_addr;

    if (pcid_supported) {
        cr3.pcid_enabled.pcid = pcid & 0xFFF;

        if (this->is_dirty) {
            this->is_dirty = false;
        } else {
            // Set bit 63 to prevent CPU from flushing a
            // valid TLB for this PCID
            cr3.pcid_enabled.no_flush = true;
        }
    }

    cr3.write();
}

void PageMap::map_range(uintptr_t virt_start, uintptr_t phys_start, size_t length, uint8_t flags,
                        CacheType cache) {
    // Greedy mapping: pick the largest page size that satisfies alignment
    // and size constraints, to reduce TLB pressure and page-table depth.
    uintptr_t virt   = virt_start;
    uintptr_t phys   = phys_start;
    size_t remaining = length;

    while (remaining > 0) {
        PageSize size = PageSize::Size4K;
        size_t step   = PAGE_SIZE_4K;

        if (support_1g_pages && is_aligned(virt, PAGE_SIZE_1G) && is_aligned(phys, PAGE_SIZE_1G) &&
            (remaining >= PAGE_SIZE_1G)) {
            size = PageSize::Size1G;
            step = PAGE_SIZE_1G;
        } else if (is_aligned(virt, PAGE_SIZE_2M) && is_aligned(phys, PAGE_SIZE_2M) &&
                   (remaining >= PAGE_SIZE_2M)) {
            size = PageSize::Size2M;
            step = PAGE_SIZE_2M;
        } else {
            size = PageSize::Size4K;
            step = PAGE_SIZE_4K;
        }

        if (remaining >= PAGE_SIZE_2M) {
            bool v_align = is_aligned(virt, PAGE_SIZE_2M);
            bool p_align = is_aligned(phys, PAGE_SIZE_2M);

            if (v_align && !p_align) {
                LOG_WARN("Virt aligned but phys unaligned! Virt: 0x%lx Phys: 0x%lx\n", virt, phys);
            }
        }

        phys = align_down(phys, step);
        virt = align_up(virt, step);

        if (!this->map(virt, phys, flags, cache, size, 0, false)) {
            PANIC("Failed to map range at virt: 0x%lx phys: 0x%lx", virt, phys);
        }

        virt += step;
        phys += step;
        remaining -= step;
    }

    // If range is huge, it's often faster to just reload the CR3 than to issue >512 `invlpg`
    if (this->is_active()) {
        if (length >= PAGE_SIZE_2M) {
            this->load();
        } else {
            // Flush all addresses individually
            for (uintptr_t v = virt_start; v < virt_start + length; v += PAGE_SIZE_4K) {
                TLB::flush(v);
            }
        }
    }
}

void PageMap::global_init() {
    bool has_pge  = arch::check_feature(FEATURE_PGE);
    bool has_pcid = arch::check_feature(FEATURE_PCID);

    bool has_la57 = arch::check_feature(FEATURE_LA57);
    bool has_smep = arch::check_feature(FEATURE_SMEP);
    bool has_smap = arch::check_feature(FEATURE_SMAP);
    bool has_pku  = arch::check_feature(FEATURE_PKU);

    support_nx       = arch::check_feature(FEATURE_NX);
    support_1g_pages = arch::check_feature(FEATURE_HUGE_PAGE);

    if (support_nx) {
        arch::Msr efer = arch::Msr::read(MSR_EFER);
        efer.value |= EFER_NXE;
        efer.write();
        LOG_INFO("Paging: NX enabled");
    }

    arch::Cr4 cr4 = arch::Cr4::read();

    if (has_pge) {
        cr4.pge = true;
    }

    if (has_smep) {
        cr4.smep = true;
    }

    if (has_pcid) {
        cr4.pcide      = true;
        pcid_supported = true;
    }

    if (has_pku) {
        cr4.pke = true;
    }

    if (has_la57) {
        if (cr4.la57) {
            max_levels = 5;
        } else {
            max_levels = 4;
        }
    } else {
        max_levels = 4;
    }

    cr4.write();

    arch::Cr0 cr0       = arch::Cr0::read();
    cr0.write_protected = true;
    cr0.write();

    init_pat();

    LOG_INFO("Paging: initialized (levels=%d 1G=%d PCID=%d)", max_levels, support_1g_pages,
             pcid_supported);
}

bool PageMap::is_active() const {
    arch::Cr3 cr3 = arch::Cr3::read();

    uintptr_t curr_phys = cr3.raw & page_mask;
    uintptr_t map_phys  = this->phys_root_addr & page_mask;

    return curr_phys == map_phys;
}

std::pair<uint8_t, CacheType> PageMap::get_flags(uintptr_t virt_addr, PageSize size) {
    int target_level = get_target_level(size);

    uint64_t* pte = get_pte(virt_addr, target_level, false);

    if ((pte == nullptr) || (*pte & FlagPresent)) {
        return std::make_pair(0, CacheType::WriteBack);
    }

    uint64_t entry    = *pte;
    size_t arch_flags = entry & ~page_mask;

    return convert_arch_flags(arch_flags, size);
}

uint8_t PageMap::get_protection_key(uintptr_t virt_addr, PageSize size) {
    int target_level = get_target_level(size);

    uint64_t* pte = get_pte(virt_addr, target_level, false);

    if (!pte || (*pte & FlagPresent)) {
        return 0;
    }

    uintptr_t entry = *pte;
    return static_cast<uint8_t>((entry >> 59) & 0xF);
}
}  // namespace kernel::memory
// NOLINTEND(bugprone-easily-swappable-parameters)
// NOLINTEND(performance-no-int-to-ptr)