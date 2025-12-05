#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <unistd.h>

#include <ucontext.h>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <signal.h>

#include "api.h"

/* --- x86_64 Page Fault Error Code --- */
// Used to determine if a fault was for a Read (0) or Write (1)
// The error code is in ucontext_t->uc_mcontext.gregs[REG_ERR]
#define FAULT_WAS_WRITE (0x2)

/* --- Page Table Entry (PTE) --- */
// Represents the state of a *Virtual Page*
typedef struct {
    int frame_index;        // Physical frame number (-1 if not in memory)
    int present;            // 1 if in physical memory, 0 otherwise
    int reference_bit;      // R bit (for Third Chance)
    int modified_bit;       // M bit (for Third Chance)
} PageTableEntry;

/* --- Frame Table --- */
// Represents the state of a *Physical Frame*
typedef struct {
    int vpn;                // Virtual Page Number occupying this frame (-1 if empty)
} FrameTableEntry;


/* --- Global State Variables --- */
// These are defined in vmm.c and used by init.c
extern enum policy_type g_policy;
extern void *g_vm_start;
extern int g_num_pages;
extern int g_num_frames;
extern int g_page_size;
extern struct mm_stats *g_stats;

extern PageTableEntry *g_page_table;
extern FrameTableEntry *g_frames;
extern int g_frames_used;

// Policy-specific data
extern int *g_fifo_queue;   // Circular buffer of VPNs
extern int g_fifo_head;     // Insertion point
extern int g_fifo_tail;     // Eviction point
extern int g_clock_hand;    // Points to next eviction candidate (frame index)

extern struct sigaction g_old_sa; // To restore the default handler

/* --- Function Prototypes --- */

/**
 * @brief The main SIGSEGV signal handler.
 * This function is the core of the memory manager, trapping faults
 * and emulating the page replacement logic.
 */
void signal_handler(int sig, siginfo_t *si, void *unused);

/* --- vmm.h --- */

// ... (keep all other lines)

/* --- Function Prototypes --- */

void signal_handler(int sig, siginfo_t *si, void *unused);

// --- Renamed function ---
static void update_page_protection_third(int vpn);
// --- New function ---
static void update_page_protection_fifo(int vpn);