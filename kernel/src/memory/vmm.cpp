#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "memory/pagemap.hpp"
#include "boot/boot.h"
#include "libs/elf.h"
#include "libs/math.hpp"
#include "task/process.hpp"
#include "memory/vmm.hpp"
#include "memory/vma.hpp"
#include "memory/cow.hpp"

namespace kernel::memory {
namespace {
// Global kernel page map used as the bootstrap address space.
PageMap kernel_pagemap          = PageMap();
VirtualAllocator virt_allocator = VirtualAllocator();

uint8_t convert_elf_flags(uint32_t p_flags) {
    uint8_t flags = Read;

    if (p_flags & PF_W) {
        flags |= Write;
    }

    if (p_flags & PF_X) {
        flags |= Execute;
    }

    return flags;
}

uintptr_t get_highest_phys_address() {
    uintptr_t max_phys = 0;

    size_t memmap_count           = memmap_request.response->entry_count;
    limine_memmap_entry** memmaps = memmap_request.response->entries;

    for (size_t i = 0; i < memmap_count; ++i) {
        uintptr_t end = memmaps[i]->base + memmaps[i]->length;

        if (end > max_phys) {
            max_phys = end;
        }
    }

    return max_phys;
}
}  // namespace

void VirtualManager::map_pagemap() {
    if (memmap_request.response == nullptr) {
        PANIC("Memmap request failed");
    }

    struct limine_memmap_entry** memmaps = memmap_request.response->entries;
    size_t memmap_count                  = memmap_request.response->entry_count;

    // Initialize global paging state (CR0/CR4/EFER tweaks, feature detection).
    PageMap::global_init();

    // Create the kernel's root page map; user maps can later clone from this.
    PageMap::create_new(&kernel_pagemap);

    for (size_t i = 0; i < memmap_count; ++i) {
        struct limine_memmap_entry* entry = memmaps[i];

        bool should_map      = false;
        CacheType cache_type = CacheType::WriteBack;

        // Decide which regions become part of the linear higher-half map.
        switch (entry->type) {
            case LIMINE_MEMMAP_USABLE:
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
            case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            case LIMINE_MEMMAP_ACPI_NVS:
            case LIMINE_MEMMAP_ACPI_TABLES:
                should_map = true;
                cache_type = CacheType::WriteBack;
                break;
            case LIMINE_MEMMAP_FRAMEBUFFER:
                should_map = true;
                cache_type = CacheType::WriteCombining;
                break;
            default:
                should_map = false;
                break;
        }

        if (should_map) {
            uintptr_t virt_addr = to_higher_half(entry->base);
            uint8_t flags       = Read;

            // Most kernel direct-map regions are writable; ACPI tables/NVS
            // remain read-only to avoid accidental corruption.
            if ((entry->type != LIMINE_MEMMAP_ACPI_TABLES) ||
                (entry->type != LIMINE_MEMMAP_ACPI_NVS)) {
                flags |= Write;
            }

            // LOG_DEBUG("VMM: mapping phys=0x%lx -> virt=0x%lx len=0x%lx type=%u", entry->base,
            //   virt_addr, entry->length, entry->type);

            kernel_pagemap.map_range(virt_addr, entry->base, entry->length, flags, cache_type);
        }
    }
}

void VirtualManager::map_kernel() {
    if ((kernel_address_request.response == nullptr) || (kernel_file_request.response == nullptr)) {
        PANIC("Kernel file or address request missing");
    }

    struct limine_file* kfile = kernel_file_request.response->executable_file;
    Elf64_Ehdr* ehdr          = static_cast<Elf64_Ehdr*>(kfile->address);

    // Verify magic
    if ((ehdr->e_ident[EI_MAG0] != ELFMAG0) || (ehdr->e_ident[EI_MAG1] != ELFMAG1) ||
        (ehdr->e_ident[EI_MAG2] != ELFMAG2) || (ehdr->e_ident[EI_MAG3] != ELFMAG3)) {
        PANIC("Kernel is not a valid ELF file!");
    }

    uintptr_t virt_base = kernel_address_request.response->virtual_base;
    uintptr_t phys_base = kernel_address_request.response->physical_base;

    Elf64_Phdr* phdr =
        reinterpret_cast<Elf64_Phdr*>(reinterpret_cast<uintptr_t>(ehdr) + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD) {
            uintptr_t seg_virt_start = phdr[i].p_vaddr;
            size_t seg_memsz         = phdr[i].p_memsz;
            uint8_t seg_flags        = convert_elf_flags(phdr[i].p_flags) | Global;

            uintptr_t start_aligned = align_down(seg_virt_start, PAGE_SIZE_4K);
            uintptr_t end_aligned   = align_up(seg_virt_start + seg_memsz, PAGE_SIZE_4K);
            size_t size_aligned     = end_aligned - start_aligned;

            uintptr_t phys_start = start_aligned - virt_base + phys_base;

            // LOG_DEBUG("VMM: mapping kernel segment v=0x%lx p=0x%lx size=0x%lx flags=0x%x",
            //   start_aligned, phys_start, size_aligned, seg_flags);

            kernel_pagemap.map_range(start_aligned, phys_start, size_aligned, seg_flags,
                                     CacheType::WriteBack);
        }
    }
}

void VirtualManager::init() {
    // Build the higher-half identity map plus kernel segments, then
    // activate it via CR3; after this, all code runs in the "real" kernel VA.
    map_pagemap();
    map_kernel();
    // This isn't CoW. I've been bamboozled T-T
    // CowManager::init();

    kernel_pagemap.load(0);

    // Reserve a large virtual region above all physical memory as the
    // kernel's "virtual heap" arena. Physical pages will be mapped here
    // on demand by the higher-level allocator.
    constexpr size_t virt_heap_size = 0x10000000000;  // 1 TiB Capacity
    uintptr_t virt_heap_start       = to_higher_half(get_highest_phys_address());
    virt_heap_start                 = align_up(virt_heap_start, PAGE_SIZE_1G);

    LOG_INFO("VMM: initializing virtual heap at 0x%lx size=0x%lx", virt_heap_start, virt_heap_size);

    virt_allocator.init(virt_heap_start, virt_heap_size);
}

PageMap* PageMap::get_kernel_map() {
    // Expose the kernel root map so new address spaces can share kernel
    // mappings (upper half) using create_new().
    return &kernel_pagemap;
}

PageMap* VirtualManager::curr_map() {
    if (cpu::CpuCoreManager::get().initialized()) {
        cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_current_core();
        task::Process* proc  = cpu->curr_thread->owner;

        return proc->map;
    }

    return &kernel_pagemap;
}

void* VirtualManager::allocate(size_t count, PageSize size, uint8_t flags, CacheType cache) {
    size_t align_bytes = 0;
    size_t step_bytes  = 0;

    switch (size) {
        case PageSize::Size4K:
            align_bytes = PAGE_SIZE_4K;
            step_bytes  = PAGE_SIZE_4K;
            break;
        case PageSize::Size2M:
            align_bytes = PAGE_SIZE_2M;
            step_bytes  = PAGE_SIZE_2M;
            break;
        case PageSize::Size1G:
            align_bytes = PAGE_SIZE_1G;
            step_bytes  = PAGE_SIZE_1G;
            break;
    }

    size_t total_bytes = count * step_bytes;

    // First, reserve a virtual range from the allocator.
    uintptr_t virt_addr = virt_allocator.alloc_region(total_bytes, align_bytes);

    if (virt_addr == 0) {
        LOG_WARN("VMM: allocate failed (virt space) count=%zu size=%u", count,
                 static_cast<unsigned>(size));
        return nullptr;
    }

    bool is_lazy = (flags & Lazy) && CowManager::initialized();

    if (is_lazy) {
        // We can't use 2MB/1GB pages for CoW because we only have a 4KB zero page.
        // So, we map everything as 4KB pages
        flags &= ~Write;
        uintptr_t zero_page = CowManager::get_zero_page_phys();
        size_t total_pages  = total_bytes / PAGE_SIZE_4K;

        for (size_t i = 0; i < total_pages; ++i) {
            curr_map()->map(virt_addr + (i * PAGE_SIZE_4K), zero_page, flags, cache,
                            PageSize::Size4K, 0, false);
        }

        curr_map()->load();
    } else {
        uintptr_t curr_virt = virt_addr;

        // Then back each page with physical memory via the kernel page map.
        for (size_t i = 0; i < count; ++i) {
            if (!curr_map()->map(curr_virt, flags, cache, size)) {
                // Roll back any mappings we already created.
                for (size_t j = 0; j < i; ++j) {
                    uintptr_t addr = virt_addr + (j * step_bytes);
                    curr_map()->unmap(addr, 0, true);
                }

                virt_allocator.free_region(virt_addr, total_bytes);

                LOG_ERROR("VMM: failed to map page at 0x%lx (rolling back)", curr_virt);
                return nullptr;
            }

            curr_virt += step_bytes;
        }
    }

    // LOG_DEBUG("VMM: allocate virt=0x%lx count=%zu size=%u", virt_addr, count,
    //   static_cast<unsigned>(size));
    return reinterpret_cast<void*>(virt_addr);
}

void VirtualManager::free(void* ptr, size_t count, PageSize size, bool free_phys) {
    uintptr_t virt_addr = reinterpret_cast<uintptr_t>(ptr);
    size_t step_bytes   = 0;

    switch (size) {
        case PageSize::Size4K:
            step_bytes = PAGE_SIZE_4K;
            break;
        case PageSize::Size2M:
            step_bytes = PAGE_SIZE_2M;
            break;
        case PageSize::Size1G:
            step_bytes = PAGE_SIZE_1G;
            break;
    }

    size_t total_bytes = count * step_bytes;

    // Tear down mappings and free backing physical pages.
    for (size_t i = 0; i < count; ++i) {
        uintptr_t addr = virt_addr + (i * step_bytes);
        curr_map()->unmap(addr, 0, free_phys);
    }

    // Return the virtual range to the allocator for reuse.
    virt_allocator.free_region(virt_addr, total_bytes);

    // LOG_DEBUG("VMM: free virt=0x%lx count=%zu size=%u", virt_addr, count,
    //   static_cast<unsigned>(size));
}

void* VirtualManager::reserve_mmio(size_t size, size_t align) {
    // Reserve a bare virtual range for MMIO; mapping to device physical
    // addresses is done separately by the caller.
    uintptr_t addr = virt_allocator.alloc_region(size, align);
    // LOG_DEBUG("VMM: reserve_mmio size=0x%zx align=0x%zx -> 0x%lx", size, align, addr);
    return reinterpret_cast<void*>(addr);
}
}  // namespace kernel::memory