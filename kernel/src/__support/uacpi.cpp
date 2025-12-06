#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/vmm.hpp"
#include "uacpi/kernel_api.h"
#include "boot/boot.h"
#include "libs/log.hpp"
#include "uacpi/log.h"
#include "uacpi/status.h"
#include "memory/pagemap.hpp"
#include "libs/math.hpp"

using namespace kernel::memory;

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    if (rsdp_request.response == nullptr) {
        PANIC("RSDP not found!");
        return UACPI_STATUS_NOT_FOUND;
    }

    uintptr_t rsdp_virt_addr = reinterpret_cast<uintptr_t>(rsdp_request.response->address);
    uintptr_t rspd_phys_addr = PageMap::get_kernel_map()->translate(rsdp_virt_addr);

    *out_rsdp_address = rspd_phys_addr;
    return UACPI_STATUS_OK;
}

// NOLINTNEXTLINE
void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    if (len == 0) {
        return nullptr;
    }

    uacpi_phys_addr aligned_addr = kernel::align_down(addr, PAGE_SIZE_4K);
    uacpi_size offset            = addr & (PAGE_SIZE_4K - 1);

    uacpi_size total_size = len + offset;

    void* vmm_base_virt_addr = VirtualManager::reserve_mmio(total_size, PAGE_SIZE_4K);

    if (vmm_base_virt_addr == nullptr) {
        return nullptr;
    }

    CacheType cache = CacheType::WriteBack;
    uint8_t flags   = Read | Write | Global;

    for (size_t i = 0; i < total_size; i += PAGE_SIZE_4K) {
        if (!PageMap::get_kernel_map()->map(reinterpret_cast<uintptr_t>(vmm_base_virt_addr),
                                            aligned_addr, flags, cache, PageSize::Size4K)) {
            PANIC("Failed to Map UACPI address 0x%lx -> %p", aligned_addr, vmm_base_virt_addr);
        }
    }

    // NOLINTNEXTLINE
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(vmm_base_virt_addr) + offset);
}

void uacpi_kernel_unmap(void* addr, uacpi_size len) {
    if (len == 0 || addr == nullptr) {
        return;
    }

    uintptr_t virt_addr = reinterpret_cast<uintptr_t>(addr);

    uacpi_size offset = virt_addr & (PAGE_SIZE_4K - 1);
    // NOLINTNEXTLINE
    void* aligned_virt_addr = reinterpret_cast<void*>(virt_addr - offset);
    uacpi_size total_size   = len + offset;

    for (size_t i = 0; i < total_size; i += PAGE_SIZE_4K) {
        PageMap::get_kernel_map()->unmap(virt_addr);
    }
}

void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char* fmt) {
    switch (lvl) {
        case UACPI_LOG_ERROR:
            LOG_ERROR(fmt);
        case UACPI_LOG_WARN:
            LOG_WARN(fmt);
        case UACPI_LOG_INFO:
        case UACPI_LOG_TRACE:
            LOG_INFO(fmt);
        case UACPI_LOG_DEBUG:
            LOG_DEBUG(fmt);
    }
}

void* uacpi_kernel_alloc(uacpi_size size) {
    return kmalloc(size);
}

void uacpi_kernel_free(void* ptr) {
    kfree(ptr);
}