# Roadmap (Current Status & Next Steps)

This document tracks what is already implemented in the kernel and what is planned next, with an emphasis on scheduling, threads, processes, and IPC.

## Implemented

- **Boot & Arch Init**
  - Limine bootloader integration and kernel entry in higher half.
  - UART16550 serial console with logging backend and panic path.
  - x86_64 CPU bootstrap: basic helpers, halt/pause/IF control.

- **Memory Management**
  - Physical Memory Manager (PMM) with bitmap + summary bitmap + stack cache.
  - Higher-half direct map (HHDM) and virtual memory manager (VMM).
  - PageMap abstraction with:
    - 4‑level (and optional 5‑level) paging.
    - Large page support (2 MiB / 1 GiB when available).
    - PCID-aware CR3 loading and TLB helpers.
  - Kernel heap on top of VMM.

- **CPU, GDT/TSS & Per‑CPU**
  - Per‑CPU `CPUData` (GDT + TSS) and `PerCPUData` anchored via GS base.
  - GDT setup for kernel/user code/data segments and 64‑bit TSS.
  - TSS with IST stacks for NMI and double-fault, plus I/O bitmap.
  - Per‑CPU initialization path (`CPUCoreManager::init_core`).

- **Interrupts & Exceptions**
  - IDT setup:
    - 256-entry IDT wired to common assembly stub (`idt.S`).
    - Dedicated IST for NMI and double fault.
  - TrapFrame definition and exception handler hand-off.
  - InterruptDispatcher:
    - Vector → handler table.
    - Default fatal handler for exceptions, warning for unhandled IRQs.
    - Spurious ACPI vector handling.
  - Double-fault handler hook (`DFHandler`).

- **APIC & IOAPIC**
  - Local APIC (LAPIC/x2APIC) abstraction:
    - xAPIC vs x2APIC detection and init.
    - IPI/broadcast IPI and INIT+SIPI for AP bring-up.
    - LAPIC timer support (one-shot, periodic, TSC-deadline).
    - Multi-source calibration (CPUID, HPET, PIT) with TSC scale.
    - Calibrated `udelay`/`mdelay` and `get_ticks_ns()`.
  - IOAPIC:
    - Discovery from MADT, MMIO mapping, and GSI range tracking.
    - Route/mask/unmask of GSIs.
    - Legacy IRQ routing with ACPI ISO handling.
  - Interrupt routing helpers:
    - Legacy IRQ and PCI GSI mapping to vectors/CPUs via IOAPIC.

- **Timers**
  - PIT helper as conservative early/fallback timebase.
  - HPET abstraction:
    - ACPI HPET table parsing and MMIO mapping.
    - Capability validation (vendor, period, ticking counter).
    - Monotonic `get_ns()` and busy-wait `ndelay`/`udelay`/`mdelay`.
    - One-shot and periodic timer configuration.
  - High-level `Timer` facade:
    - Policy: prefer calibrated LAPIC, then HPET, then PIT.

- **ACPI & Topology**
  - ACPI bootstrap via uACPI with early table buffer.
  - MADT parsing into linked lists:
    - LAPIC, IOAPIC, ISO, x2APIC entries for later routing.

- **I/O & Legacy Controllers**
  - Port I/O helpers (`in`/`out`/`io_wait`).
  - Legacy PIC (8259) remap and mask-all for IOAPIC-based systems.

- **Scheduling & Threads**
  - Saves register state, stack pointer, and per-thread metadata.
  - Priority-based Round Robin scheduling
  - Tied totimer ticks (periodic interrupts).
  
- **Processes & Address Spaces**
  - Owns a `PageMap` (CR3), list of threads, and resources.

## In Progress / Planned

- **Processes & Address Spaces**
  - Per-process address spaces:
    - Use existing `PageMap::create_new` to fork user/kernel halves.
  - Basic process lifecycle:
    - Create, exec/load ELF into user space, and destroy.

- **Syscalls & User Mode**
  - System call entry:
    - Configure `MSR_LSTAR` and friends for `syscall/sysret`.
    - Trap into a syscall dispatcher in kernel space.
  - Core syscall set:
    - Yield/sleep, memory map/unmap, and IPC primitives.
  - Transition to Ring 3:
    - Build initial user stack and jump via `sysret` or `iretq`.

- **IPC (Inter-Process Communication)**
  - Message-passing primitives:
    - `send(pid, msg)` and `recv()` with blocking semantics.
  - Port or endpoint abstraction:
    - Named or capability-based channels for system services.
  - Optimization:
    - Large-message support via page remapping (zero-copy IPC).

- **User-Space Services**
  - Init process:
    - Launched from a Limine module or ramdisk.
  - User-space drivers:
    - Keyboard and other simple devices using I/O-port whitelisting.
  - VFS service:
    - User-space file system server and basic file syscalls over IPC.

This roadmap will evolve as core kernel primitives (scheduling, processes, IPC) solidify and more functionality migrates out of Ring 0 into user-space services.