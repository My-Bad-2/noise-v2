#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/vmm.hpp"
#include "uacpi/kernel_api.h"
#include "boot/boot.h"
#include "libs/log.hpp"
#include "uacpi/log.h"
#include "uacpi/platform/arch_helpers.h"
#include "uacpi/status.h"
#include "memory/pagemap.hpp"
#include "libs/math.hpp"
#include "libs/spinlock.hpp"
#include "hal/timer.hpp"

using namespace kernel::memory;

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    if (rsdp_request.response == nullptr) {
        PANIC("RSDP not found!");
        return UACPI_STATUS_NOT_FOUND;
    }

    uintptr_t rsdp_virt_addr = reinterpret_cast<uintptr_t>(rsdp_request.response->address);
    uintptr_t rspd_phys_addr = VirtualManager::curr_map()->translate(rsdp_virt_addr);

    *out_rsdp_address = rspd_phys_addr;
    return UACPI_STATUS_OK;
}

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
        if (!VirtualManager::curr_map()->map(reinterpret_cast<uintptr_t>(vmm_base_virt_addr) + i,
                                             aligned_addr + i, flags, cache, PageSize::Size4K)) {
            PANIC("Failed to Map UACPI address 0x%lx -> %p", aligned_addr, vmm_base_virt_addr);
        }
    }

    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(vmm_base_virt_addr) + offset);
}

void uacpi_kernel_unmap(void* addr, uacpi_size len) {
    if (len == 0 || addr == nullptr) {
        return;
    }

    uintptr_t virt_addr = reinterpret_cast<uintptr_t>(addr);

    uacpi_size offset = virt_addr & (PAGE_SIZE_4K - 1);

    void* aligned_virt_addr = reinterpret_cast<void*>(virt_addr - offset);
    uacpi_size total_size   = len + offset;

    for (size_t i = 0; i < total_size; i += PAGE_SIZE_4K) {
        VirtualManager::curr_map()->unmap(virt_addr + i);
    }
}

void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char* fmt) {
    switch (lvl) {
        case UACPI_LOG_ERROR:
            LOG_ERROR(fmt);
            break;
        case UACPI_LOG_WARN:
            LOG_WARN(fmt);
            break;
        case UACPI_LOG_INFO:
        case UACPI_LOG_TRACE:
            LOG_INFO(fmt);
            break;
        case UACPI_LOG_DEBUG:
            LOG_DEBUG(fmt);
            break;
    }
}

void* uacpi_kernel_alloc(uacpi_size size) {
    return kmalloc(size);
}

void uacpi_kernel_free(void* ptr) {
    kfree(ptr);
}

uacpi_handle uacpi_kernel_create_mutex() {
    return reinterpret_cast<uacpi_handle>(new kernel::SpinLock);
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
    delete reinterpret_cast<kernel::SpinLock*>(handle);
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
    kernel::SpinLock* lock = reinterpret_cast<kernel::SpinLock*>(handle);
    bool locked            = false;

    if (timeout == 0xFFFF) {
        lock->lock();
        return UACPI_STATUS_OK;
    } else {
        locked = lock->try_lock();
    }

    return locked ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    kernel::SpinLock* lock = reinterpret_cast<kernel::SpinLock*>(handle);
    lock->unlock();
}

uacpi_handle uacpi_kernel_create_event() {
    return nullptr;
}

void uacpi_kernel_free_event(uacpi_handle handle) {}

uacpi_handle uacpi_kernel_create_spinlock() {
    return reinterpret_cast<uacpi_handle>(new kernel::IrqLock);
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    delete reinterpret_cast<kernel::IrqLock*>(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    reinterpret_cast<kernel::IrqLock*>(handle)->lock();
    return 0;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags) {
    reinterpret_cast<kernel::IrqLock*>(handle)->unlock();
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    kernel::hal::Timer::udelay(usec);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    kernel::hal::Timer::mdelay(static_cast<uint32_t>(msec));
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot() {
    return 0;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle, uacpi_size, uacpi_u8) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle, uacpi_size, uacpi_u16) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle, uacpi_size, uacpi_u32) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle, uacpi_size, uacpi_u8*) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle, uacpi_size, uacpi_u16*) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle, uacpi_size, uacpi_u32*) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle, uacpi_u16) {
    return UACPI_TRUE;
}

uacpi_thread_id uacpi_kernel_get_thread_id() {
    return reinterpret_cast<uacpi_thread_id>(1);
}

uacpi_status uacpi_kernel_wait_for_work_completion() {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type, uacpi_work_handler, uacpi_handle) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address, uacpi_handle*) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_kernel_pci_device_close(uacpi_handle) {}

void uacpi_kernel_signal_event(uacpi_handle) {}

void uacpi_kernel_reset_event(uacpi_handle) {}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request*) {
    return UACPI_STATUS_UNIMPLEMENTED;
}