# Virtual Memory Manager

A user-space virtual memory manager implemented in C for **PSU CMPSC 473 (Operating Systems) — Fall 2025, Project 3**.

The system handles page-fault-driven memory allocation and supports two configurable page replacement policies: **FIFO** and **Third Chance**.

## Features

- Signal-based page fault handling via `SIGSEGV` / `mmap`
- Page table and frame table management
- Two page replacement policies:
  - **Policy 1 — FIFO:** Evicts the oldest page in physical memory
  - **Policy 2 — Third Chance:** Extends clock-based replacement by giving pages up to three reference chances before eviction
- Comprehensive stats logging (page faults, evictions, reads/writes per operation)
- Sample inputs and expected outputs included for validation

## Repository Structure

```
.
├── main.c                  # Entry point — reads input file, drives read/write ops
├── Makefile                # Build system
├── api.h                   # Public API — mm_read, mm_write, mm_stats
├── libmemmanager/
│   ├── vmm.c / vmm.h       # Core VMM: page table, frame table, fault handler
│   ├── init.c              # Initialization (mmap, signal handler setup)
│   ├── logger.c            # Stats logging
│   └── Makefile
├── sample_input/           # 12 test input files
├── sample_output/          # Expected outputs for each (policy × frame_count × input)
└── tester                  # Test runner script
```

## Build & Run

```bash
make

# Usage: ./main <replacement_policy> <num_frames> <input_file>
#   Policy 1 = FIFO
#   Policy 2 = Third Chance

./main 1 4 sample_input/input_1      # FIFO, 4 frames
./main 2 8 sample_input/input_3      # Third Chance, 8 frames
```

## Testing

```bash
./tester
```

The tester runs all combinations of `{policy 1, 2} × {frame counts} × {input_1 … input_12}` and diffs output against `sample_output/`.

## Input Format

Each line in an input file is either a **read** or **write** operation:

```
read  <page_number> <offset>
write <page_number> <offset> <value>
```

## Implementation Notes

- Physical memory is allocated as a contiguous `mmap` region; virtual pages are mapped into it on demand.
- The page table is a flat array indexed by virtual page number (VPN).
- Frame eviction writes dirty pages back before unmapping them to preserve correctness.
- Stats include per-operation fault type, evicted VPN, and cumulative fault counts.
