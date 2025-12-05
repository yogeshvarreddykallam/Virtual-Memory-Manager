#pragma once

#include <stdint.h>

// Statistics
struct mm_log {
    int virt_page;
    int fault_type;
    int evicted_page;
    int write_back;
    uint64_t phy_addr;
};

struct mm_stats {
    struct mm_log *log;
    uint64_t counter;
};

// Policy type
enum policy_type {
    MM_FIFO = 1,  // FIFO Replacement Policy
    MM_THIRD = 2, // Third Chance Replacement Policy
};

// APIs
void mm_init(enum policy_type policy, void *vm, int vm_size,
             int num_frames, int page_size, struct mm_stats *stats);
void mm_finish();

// This is already implemented in logger.c file. Therefore, no need to implement.
// Call this function appropriately
void mm_logger(struct mm_stats *stats, int virt_page, int fault_type,
               int evicted_page, int write_back, uint64_t phy_addr);
