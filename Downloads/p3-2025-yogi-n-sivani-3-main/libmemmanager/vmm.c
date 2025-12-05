



#include "vmm.h"
#define REG_ERR 19
/* --- Helper Prototypes --- */
static int evict_page_fifo();
static int evict_page_third();
static void update_page_protection_third(int vpn);
static void* get_page_address(int vpn);


/**
 * @brief The main SIGSEGV signal handler. Traps faults and manages pages.
 */
void signal_handler(int sig, siginfo_t *si, void *ucontext) {
    
    // 1. Identify faulting address and VPN
    void *fault_addr = si->si_addr;
    
    // Check if the fault is within our managed VM region
    if (fault_addr < g_vm_start || fault_addr >= (g_vm_start + g_num_pages * g_page_size)) {
        // This is a "real" segfault, not one of ours.
        // Restore default handler and re-raise the signal.
        sigaction(SIGSEGV, &g_old_sa, NULL);
        raise(SIGSEGV);
        return;
    }

    ptrdiff_t offset_in_vm = (char *)fault_addr - (char *)g_vm_start;
    int vpn = offset_in_vm / g_page_size;
    int offset = offset_in_vm % g_page_size;

    // 2. Identify access type (Read vs. Write) from the ucontext
    ucontext_t *ctx = (ucontext_t *)ucontext;
    greg_t err_code = ctx->uc_mcontext.__gregs[REG_ERR];
    bool is_write = (err_code & FAULT_WAS_WRITE);

    // 3. Initialize logger variables
    int fault_type = 0;
    int evicted_vpn = -1;
    int write_back = 0;
    uint64_t phy_addr = 0;
    
    PageTableEntry *pte = &g_page_table[vpn];

    /* --- Fault Logic --- */

    if (!pte->present) {
        // --- Case 1: Page is NOT in physical memory (True Page Fault) ---
        
        fault_type = is_write ? 1 : 0;
        int frame_idx;

        if (g_frames_used < g_num_frames) {
            // A) Physical memory has free frames
            frame_idx = g_frames_used;
            g_frames_used++;
        } else {
            // B) Physical memory is full, must evict
            if (g_policy == MM_FIFO) {
                frame_idx = evict_page_fifo();
            } else {
                frame_idx = evict_page_third();
            }

            evicted_vpn = g_frames[frame_idx].vpn;
            PageTableEntry *victim_pte = &g_page_table[evicted_vpn];

            // Check if victim page was modified
            if (victim_pte->modified_bit) {
                write_back = 1;
            }

            // Evict: Reset victim's PTE and protect its page
            victim_pte->present = 0;
            victim_pte->frame_index = -1;
            victim_pte->reference_bit = 0;
            victim_pte->modified_bit = 0;
            mprotect(get_page_address(evicted_vpn), g_page_size, PROT_NONE);
        }

        // Load new page into the frame
        g_frames[frame_idx].vpn = vpn;
        pte->frame_index = frame_idx;
        pte->present = 1;
        pte->reference_bit = 1;
        pte->modified_bit = is_write ? 1 : 0;
        
        // Update FIFO queue if necessary
        if (g_policy == MM_FIFO) {
            g_fifo_queue[g_fifo_head] = vpn;
            g_fifo_head = (g_fifo_head + 1) % g_num_frames;
        }

    } else {
        // --- Case 2: Page IS in physical memory (Protection Fault) ---
        // This fault is for tracking R/M bits for Third Chance
        // or a write to a read-only page (FIFO or Third Chance).

        if (is_write && !pte->modified_bit) {
            // Fault Type 2: Write to a Read-Only page
            // (e.g., R=1, M=0 in Third Chance, or first write in FIFO)
            fault_type = 2;
            pte->modified_bit = 1;
        } else if (!is_write && !pte->reference_bit) {
            // Fault Type 3: Read to a non-referenced page (R=0)
            fault_type = 3;
        } else if (is_write && !pte->reference_bit) {
            // Fault Type 4: Write to a non-referenced page (R=0)
            fault_type = 4;
            pte->modified_bit = 1;
        }
        // Note: A read to a R=1 page won't fault.
        // A write to a R=1, M=1 page won't fault.
        
        pte->reference_bit = 1; // Any access sets the reference bit
    }

    // 4. Update mprotect permissions based on new state
// 4. Update mprotect permissions based on new state
    if (g_policy == MM_FIFO) {
        update_page_protection_fifo(vpn);
    } else {
        update_page_protection_third(vpn);
    }

    // 5. Log the event
    phy_addr = (pte->frame_index * g_page_size) + (offset_in_vm % g_page_size);
    mm_logger(g_stats, vpn, fault_type, evicted_vpn, write_back, phy_addr);
}


/* --- Policy-Specific Helper Functions --- */

/**
 * @brief Finds a victim frame using the FIFO policy.
 * @return The physical frame index of the victim.
 */
static int evict_page_fifo() {
    int victim_vpn = g_fifo_queue[g_fifo_tail];
    g_fifo_tail = (g_fifo_tail + 1) % g_num_frames;
    return g_page_table[victim_vpn].frame_index;
}

/**
 * @brief Finds a victim frame using the Third Chance policy.
 * @return The physical frame index of the victim.
 */
static int evict_page_third() {
    while (true) {
        // --- Pass 1: Find (R=0, M=0) ---
        for (int i = 0; i < g_num_frames; i++) {
            int vpn = g_frames[g_clock_hand].vpn;
            PageTableEntry *pte = &g_page_table[vpn];

            if (pte->reference_bit == 0 && pte->modified_bit == 0) {
                // Case (a): Evict immediately
                int victim_frame = g_clock_hand;
                g_clock_hand = (g_clock_hand + 1) % g_num_frames;
                return victim_frame;
            }
            g_clock_hand = (g_clock_hand + 1) % g_num_frames;
        }

        // --- Pass 2: Find (R=0, M=1), reset all R bits ---
        for (int i = 0; i < g_num_frames; i++) {
            int vpn = g_frames[g_clock_hand].vpn;
            PageTableEntry *pte = &g_page_table[vpn];

            if (pte->reference_bit == 0 && pte->modified_bit == 1) {
                // Case (c) after R bit was cleared: Evict
                int victim_frame = g_clock_hand;
                g_clock_hand = (g_clock_hand + 1) % g_num_frames;
                return victim_frame;
            }
            
            // Give 2nd/3rd chance: Reset R bit
            if (pte->reference_bit == 1) {
                pte->reference_bit = 0;
                update_page_protection_third(vpn); // Update mprotect to PROT_NONE
            }
            g_clock_hand = (g_clock_hand + 1) % g_num_frames;
        }
        // Loop again. After Pass 2, all pages will have R=0.
        // The next loop (Pass 1) will find any (R=0, M=0) pages (formerly R=1, M=0).
        // If none, the next (Pass 2) will find (R=0, M=1) pages (formerly R=1, M=1).
    }
}

/**
 * @brief Sets mprotect permissions for a VPN based on its R/M bits.
 * (Used for Third Chance policy)
 */
/**
 * @brief Sets mprotect permissions for FIFO policy.
 */
static void update_page_protection_fifo(int vpn) {
    PageTableEntry *pte = &g_page_table[vpn];
    void *addr = get_page_address(vpn);

    if (!pte->present) {
        mprotect(addr, g_page_size, PROT_NONE);
    } else if (pte->modified_bit) {
        // If it's dirty, it must be Read/Write
        mprotect(addr, g_page_size, PROT_READ | PROT_WRITE);
    } else {
        // If it's clean, it's Read-Only
        mprotect(addr, g_page_size, PROT_READ);
    }
}


static void update_page_protection_third(int vpn) {
    PageTableEntry *pte = &g_page_table[vpn];
    void *addr = get_page_address(vpn);

    if (!pte->present) {
        mprotect(addr, g_page_size, PROT_NONE);
    } else if (pte->reference_bit == 0) {
        // (R=0, M=0) or (R=0, M=1)
        // We must fault on *any* access to set R=1
        mprotect(addr, g_page_size, PROT_NONE);
    } else if (pte->reference_bit == 1 && pte->modified_bit == 0) {
        // (R=1, M=0)
        // Allow reads, fault on writes
        mprotect(addr, g_page_size, PROT_READ);
    } else {
        // (R=1, M=1)
        // Allow all access
        mprotect(addr, g_page_size, PROT_READ | PROT_WRITE);
    }
}

/**
 * @brief Helper to get the base address of a virtual page.
 */
static void* get_page_address(int vpn) {
    return (char*)g_vm_start + (vpn * g_page_size);
}