# ECE391 Fall 2024 — RISC-V Operating System Project

This repository represents a complete, working RISC-V–based operating system built incrementally across multiple subsystem milestones.  
The system runs on QEMU, uses Sv39 paging, supports interrupts, UART, threading, processes, ELF program loading, a filesystem, and preemptive multitasking.  
Each checkpoint expands the kernel with more sophisticated OS layers until the system can boot, schedule threads, manage memory, load programs, interface with virtio devices, and run user applications like `hello`, `trek`, and additional tests on top of a functioning system call interface.

---

## Overview of the System

The OS is built for a 64-bit RISC-V machine and implements:

- **Interrupt-driven device I/O** via UART and the Platform-Level Interrupt Controller (PLIC).  
- **Cooperative and preemptive threading**, with context switching implemented in RISC-V assembly.  
- **Sv39 virtual memory**, including 1:1 kernel mappings, page allocation, and per-process address spaces.  
- **A simplified filesystem** backed by a virtio block device, supporting open/read/write/seek semantics.  
- **An ELF program loader** capable of loading user programs using I/O interfaces rather than raw memory.  
- **A process abstraction** with fork, exec, wait, sleep, and signal-wakeup behavior.  
- **A scheduler** that handles both cooperative yielding and timer-based preemption.  

The architecture resembles a lightweight Unix-like kernel with modernized abstractions where appropriate.

---

# MP1 — Starry Night Screensaver  
*(RISC-V assembly; framebuffer drawing)*  
The first major component was a graphical screensaver running entirely in assembly. Here the system draws stars, windows, and timed beacons using a 640×480 16-bit framebuffer.  
Key contributions included:  
- Direct pixel plotting via calculated (x,y) offsets.  
- Dynamic addition/removal of objects.  
- A timer-driven blinking beacon image.  
:contentReference[oaicite:0]{index=0}

---

# MP2 — Interrupts, UART, Threads

## Checkpoint 1 — Interrupt-Driven UART + PLIC  
This stage established the device layer:  
- Full PLIC support: priority, pending bits, enable/disable, thresholds, claim/complete logic.  
- UART driver with TX/RX ring buffers, interrupt-driven I/O, and ISR integration.  
- An external interrupt handler that dispatches device IRQs to their registered ISRs.  
:contentReference[oaicite:1]{index=1}

## Checkpoint 2 — Cooperative Kernel Threads  
The OS gained thread-level concurrency:  
- Thread creation, join, yield, and exit semantics.  
- A ready list and a WAITING state backed by condition variables.  
- Round-robin scheduler implemented in C + assembly (context switch in `thrasm.s`).  
- Device-driven wakeups for timed operations (Rule30 screensaver + Star Trek UART I/O).  
:contentReference[oaicite:2]{index=2}

---

# MP3 — Illinix 391 Operating System

## Checkpoint 1 — virtio Block Driver, Filesystem, ELF Loader  
The kernel expanded into a multi-module OS:  
- Completed **virtio block driver** (`vioblk`): attach, open/close, read/write, and interrupt service.  
- Implemented **filesystem abstractions**: mount, open, read, write, ioctl, and in-memory “lit” devices.  
- Implemented **ELF program loading** of PT_LOAD segments into the kernel’s executable region.  
- System capable of launching user programs (`hello`, `trek`) through the FS + ELF interface.  
:contentReference[oaicite:3]{index=3}

## Checkpoint 2 — Virtual Memory & Process Abstraction  
A full memory subsystem was introduced:  
- Sv39 paging with kernel mappings, page allocation, mapping, and flag control.  
- Process structures, user/supervisor transitions, traps, and system call entry/exit.  
- Exec, exit, wait, read/write, open/close, and other syscalls backed by the filesystem + VMA system.  
:contentReference[oaicite:4]{index=4}

## Checkpoint 3 — Fork, Locks, Reference Counting, Preemption  
The kernel reached full OS functionality:  
- **fork** with page-table duplication and parent/child relationships.  
- **sleep/wakeup**, blocking syscalls, and condition-based waiting.  
- **Reference counting** for shared resources like file descriptors or in-memory buffers.  
- **Preemptive scheduling** via timer interrupts, allowing true multitasking across processes.  
:contentReference[oaicite:5]{index=5}
