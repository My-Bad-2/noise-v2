# Microkernel Development Roadmap

This roadmap outlines the development stages for a 64-bit microkernel using the Limine bootloader. The architecture focuses on minimalism in Ring 0, delegating drivers and services to user space.

## Phase 1: Bootstrapping & Infrastructure
*Goal: Establish a build environment and get the CPU into a known state.*

- [X] **Toolchain Setup**
    - [X] Build/Install Cross-Compiler (`x86_64-elf-gcc`, `ld`).
    - [X] Configure `make` or `cmake` build system.
- [X] **Limine Integration**
    - [X] Configure `limine.cfg` (Limine protocol).
- [X] **Kernel Entry**
    - [X] Write `linker.ld` (Kernel logical address layout).
    - [X] Implement `_start` in Assembly (Validate Limine handshake, setup stack).
    - [X] Implement `kmain()` entry point.
- [X] **Basic Output**
    - [X] Initialize UART (COM1 at `0x3F8`) for serial logging.
    - [X] Implement `printf` utilizing UART. Use LLVM-libc for baremetal environment?

## Phase 2: Memory Management
*Goal: Manage physical RAM and establish Virtual Memory for isolation.*

- [X] **Physical Memory Manager (PMM)**
    - [X] Parse Limine Memory Map (`limine_memmap_request`).
    - [X] Implement Bitmap with Hot Cache stack for 4KiB page tracking.
- [ ] **Virtual Memory Manager (VMM)**
    - [X] Retrieve HHDM (Higher Half Direct Map) offset from Limine.
    - [X] Implement x86_64 4-Level Paging (PML4, PDP, PD, PT). Support LA57?
    - [X] Create `map` and `unmap` functions.
    - [X] Bootstrap the Kernel Page Table (Higher Half mapping).
- [X] **Kernel Heap**
    - [X] Implement a Heap Allocator.
    - [X] Expose `kmalloc` and `kfree`.

## Phase 3: CPU & Interrupts
*Goal: Handle hardware exceptions and hardware timers.*

- [X] **Global Descriptor Table (GDT)**
    - [X] Construct GDT with Kernel/User Code & Data segments.
    - [X] Setup TSS (Task State Segment) for interrupt stack switching.
- [X] **Interrupt Descriptor Table (IDT)**
    - [X] Define IDT structure.
    - [X] Implement Assembly ISR stubs (Save/Restore registers).
    - [X] Handle Critical Exceptions (`#GP`, `#PF`, `#DF`).
- [ ] **Hardware Interrupts**
    - [ ] Mask Legacy PIC (8259).
    - [ ] Initialize LAPIC (Local APIC) per core.
    - [ ] Calibrate APIC Timer for scheduling ticks.

## Phase 4: Microkernel Core (Scheduling & IPC)
*Goal: The core logic allowing multiple tasks to exist and communicate.*

- [ ] **Process/Thread Management**
    - [ ] Define `TCB` (Thread Control Block) and `PCB` (Process Control Block).
    - [ ] Implement Context Switching logic (Assembly: swap `rsp`, `cr3`, registers).
- [ ] **Scheduler**
    - [ ] Implement Round Robin scheduler hooked to APIC Timer.
    - [ ] Implement Thread States (`READY`, `RUNNING`, `BLOCKED`).
- [ ] **Inter-Process Communication (IPC)**
    - [ ] **Message Passing:** Implement `send(pid, msg)` and `recv()`.
    - [ ] **Ports:** Implement port-based addressing for services.
    - [ ] **Optimization:** Implement Single-Copy or Zero-Copy (page mapping) for large messages.

## Phase 5: User Space Transition
*Goal: Drop privileges and execute code in Ring 3.*

- [ ] **System Call Interface**
    - [ ] Configure `MSR_LSTAR` for `syscall`/`sysret` instructions.
    - [ ] Implement Syscall Dispatcher (Handler for IPC, Memory, Yield).
- [ ] **ELF Loader**
    - [ ] Parse ELF headers to load binaries into virtual memory.
- [ ] **Ring 3 Entry**
    - [ ] Craft the initial User Stack.
    - [ ] Execute `sysret` or `iretq` to jump to User Entry point.

## Phase 6: User Space Services
*Goal: Move traditional kernel functions out of Ring 0.*

- [ ] **Init Process**
    - [ ] Load an `init` binary from Limine Modules (Ramdisk).
- [ ] **VFS Server**
    - [ ] Implement a file system server in user space.
    - [ ] Implement `open`, `read`, `write` via IPC.
- [ ] **Drivers**
    - [ ] Implement I/O port whitelisting (Bitmap in TSS or syscall).
    - [ ] Create a basic user-mode keyboard driver.