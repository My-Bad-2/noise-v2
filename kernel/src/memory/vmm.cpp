#include "hal/smp_manager.hpp"
#include "boot/boot.h"
#include "libs/elf.h"
#include "libs/math.hpp"
#include "memory/vmm.hpp"
#include "memory/pagemap.hpp"
#include "memory/pcid_manager.hpp"
#include "memory/vma.hpp"
#include "task/process.hpp"

namespace kernel::memory {
namespace {
// Global kernel page map used as the bootstrap address space.
PageMap kernel_pagemap     = PageMap();
VirtualMemoryAllocator vma = VirtualMemoryAllocator();

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
    uintptr_t virt_heap_start = to_higher_half(get_highest_phys_address());
    virt_heap_start           = align_up(virt_heap_start, PAGE_SIZE_1G);

    LOG_INFO("VMM: initializing virtual heap at 0x%lx", virt_heap_start);

    vma.init(virt_heap_start);
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

size_t VirtualManager::page_size_to_bytes(PageSize size, size_t count) {
    switch (size) {
        case PageSize::Size1G:
            return count * PAGE_SIZE_1G;
        case PageSize::Size2M:
            return count * PAGE_SIZE_2M;
        case PageSize::Size4K:
        default:
            return count * PAGE_SIZE_4K;
    }
}

void* VirtualManager::allocate(size_t count, PageSize size, uint8_t flags, CacheType cache) {
    size_t total_bytes = page_size_to_bytes(size, count);
    return vma.allocate(total_bytes, flags, cache);
}

void VirtualManager::free(void* ptr, bool free_phys) {
    vma.free(ptr, free_phys);
}

void* VirtualManager::reserve_mmio(size_t size, size_t align) {
    return vma.reserve(size, align, 0);
}

ScopedAddressSpaceSwitch::ScopedAddressSpaceSwitch(task::Process* proc) {
    cpu::CpuCoreManager& manager = cpu::CpuCoreManager::get();
    cpu::PerCpuData* cpu         = manager.get_current_core();

    task::Process* curr = cpu->curr_thread->owner;
    this->old_map       = curr->map;
    this->old_pcid      = cpu->pcid_manager->get_pcid(curr);

    PageMap* new_map = proc->map;
    uint16_t pcid    = cpu->pcid_manager->get_pcid(proc);

    if (this->old_map != new_map) {
        new_map->load(pcid);
    }
}

ScopedAddressSpaceSwitch::~ScopedAddressSpaceSwitch() {
    PageMap* curr_map = VirtualManager::curr_map();

    if (curr_map != old_map) {
        old_map->load(this->old_pcid);
    }
}
}  // namespace kernel::memory