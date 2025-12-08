#include "vmm.h"

#define REG_ERR 19

static int evict_page_fifo();
static int evict_page_third();
static void update_page_protection_third(int vpn);
static void update_page_protection_fifo(int vpn);
static void* find_pgaddr(int vpn);

// Signal Handler for SIGSEGV
void signal_handler(int sig, siginfo_t *si, void *ucontext) {
    void *fault_addr = si->si_addr;

    // Check if fault is outside our managed VM region
    if (fault_addr < vm_begin || fault_addr >= (vm_begin + num_of_pages * pgsize)) {
        // Restore default handler and re-raise
        sigaction(SIGSEGV, &old_sa, NULL);
        raise(SIGSEGV);
        return;
    }

    // Calculate virtual page number and offset
    ptrdiff_t offset_in_vm = (char *)fault_addr - (char *)vm_begin;
    int vpn = offset_in_vm / pgsize;
    int offset = offset_in_vm % pgsize;

    // Identify the access type (read or write)
    ucontext_t *ctx = (ucontext_t *)ucontext;
    greg_t err_code = ctx->uc_mcontext.__gregs[REG_ERR];
    bool is_write = (err_code & FAULT_WAS_WRITE);

    int fault_type = 0;
    int evicted_vpn = -1;
    int write_back = 0;
    uint64_t phy_addr = 0;

    PageTableEntry *pte = &pt[vpn];

    if (!pte->present) {
        // Page fault: page not in physical memory
        fault_type = is_write ? 1 : 0;

        int fr_idx;
        if (fr_used < num_of_fr) {
            // Free frame available
            fr_idx = fr_used;
            fr_used++;
        } else {
            // Need to evict a page
            if (type == MM_FIFO) {
                fr_idx = evict_page_fifo();
            } else {
                fr_idx = evict_page_third();
            }

            evicted_vpn = fr[fr_idx].vpn;
            PageTableEntry *evicted_pte = &pt[evicted_vpn];

            if (evicted_pte->mod_bit) {
                write_back = 1;
            }

            // Evict the page
            evicted_pte->present = 0;
            evicted_pte->frame_no = -1;
            evicted_pte->ref_bit = 0;
            evicted_pte->mod_bit = 0;
            mprotect(find_pgaddr(evicted_vpn), pgsize, PROT_NONE);
        }

        // Bring in the new page
        fr[fr_idx].vpn = vpn;
        pte->frame_no = fr_idx;
        pte->present = 1;
        pte->ref_bit = 1;
        pte->mod_bit = is_write ? 1 : 0;

        // For FIFO, add to queue
        if (type == MM_FIFO) {
            queue[head] = vpn;
            head = (head + 1) % num_of_fr;
        }
    } else {
        // Page is present - handling permission faults or reference tracking
        if (is_write && !pte->mod_bit) {
            // Write to a read-only page
            fault_type = 2;
            pte->mod_bit = 1;
            pte->ref_bit = 1;
        } else if (!is_write && !pte->ref_bit) {
            // Read access to track reference bit (for third chance)
            fault_type = 3;
            pte->ref_bit = 1;
        } else if (is_write && !pte->ref_bit) {
            // Write access to track both reference and modified bits
            fault_type = 4;
            pte->ref_bit = 1;
            pte->mod_bit = 1;
        } else {
            // This shouldn't happen if mprotect is set correctly
            return;
        }
    }

    // Update memory protection based on policy
    if (type == MM_FIFO) {
        update_page_protection_fifo(vpn);
    } else {
        update_page_protection_third(vpn);
    }

    // Calculate physical address
    phy_addr = (pte->frame_no * pgsize) + offset;

    // Log the event
    mm_logger(stats, vpn, fault_type, evicted_vpn, write_back, phy_addr);
}

static int evict_page_fifo() {
    int vpn_to_evict = queue[tail];
    tail = (tail + 1) % num_of_fr;
    return pt[vpn_to_evict].frame_no;
}

static int evict_page_third() {
    // Third chance algorithm with proper 3 passes:
    // Pass 1: Evict R=0,M=0; Clear R=1; Skip R=0,M=1 (1st chance)
    // Pass 2: Evict R=0,M=0; Skip R=0,M=1 (2nd chance)
    // Pass 3: Evict R=0,M=1 or R=0,M=0 (3rd chance exhausted)

    // Pass 1: Look for R=0,M=0 or clear R=1
    for (int i = 0; i < num_of_fr; i++) {
        int vpn = fr[clk].vpn;
        PageTableEntry *pte = &pt[vpn];

        if (pte->ref_bit == 0 && pte->mod_bit == 0) {
            // Found clean victim
            int victim_frame = clk;
            clk = (clk + 1) % num_of_fr;
            return victim_frame;
        } else if (pte->ref_bit == 1) {
            // Clear reference bit (give second chance)
            pte->ref_bit = 0;
            update_page_protection_third(vpn);
        }
        // Skip R=0, M=1 (give third chance)

        clk = (clk + 1) % num_of_fr;
    }

    // Pass 2: Look for R=0,M=0 (from cleared refs)
    for (int i = 0; i < num_of_fr; i++) {
        int vpn = fr[clk].vpn;
        PageTableEntry *pte = &pt[vpn];

        if (pte->ref_bit == 0 && pte->mod_bit == 0) {
            // Found clean victim
            int victim_frame = clk;
            clk = (clk + 1) % num_of_fr;
            return victim_frame;
        } else if (pte->ref_bit == 1) {
            // Clear if set again
            pte->ref_bit = 0;
            update_page_protection_third(vpn);
        }
        // Still skip R=0, M=1

        clk = (clk + 1) % num_of_fr;
    }

    // Pass 3: Evict anything (R=0,M=1 has exhausted its 3 chances)
    for (int i = 0; i < num_of_fr; i++) {
        int vpn = fr[clk].vpn;
        PageTableEntry *pte = &pt[vpn];

        if (pte->ref_bit == 0) {
            // Evict (either M=0 or M=1)
            int victim_frame = clk;
            clk = (clk + 1) % num_of_fr;
            return victim_frame;
        } else if (pte->ref_bit == 1) {
            // Clear and continue
            pte->ref_bit = 0;
            update_page_protection_third(vpn);
        }

        clk = (clk + 1) % num_of_fr;
    }

    // Fallback: evict current position
    int victim = clk;
    clk = (clk + 1) % num_of_fr;
    return victim;
}

static void update_page_protection_fifo(int vpn) {
    PageTableEntry *pte = &pt[vpn];
    void *addr = find_pgaddr(vpn);

    if (!pte->present) {
        mprotect(addr, pgsize, PROT_NONE);
    } else if (pte->mod_bit) {
        // Page has been written to, give full read-write access
        mprotect(addr, pgsize, PROT_READ | PROT_WRITE);
    } else {
        // Page is read-only initially
        mprotect(addr, pgsize, PROT_READ);
    }
}

static void update_page_protection_third(int vpn) {
    PageTableEntry *pte = &pt[vpn];
    void *addr = find_pgaddr(vpn);

    if (!pte->present) {
        mprotect(addr, pgsize, PROT_NONE);
    } else if (pte->ref_bit == 0) {
        // Reference bit is 0, need to catch next access
        mprotect(addr, pgsize, PROT_NONE);
    } else if (pte->ref_bit == 1 && pte->mod_bit == 0) {
        // Reference bit is 1, not modified yet - read-only
        mprotect(addr, pgsize, PROT_READ);
    } else {
        // Reference bit is 1 and modified - full access
        mprotect(addr, pgsize, PROT_READ | PROT_WRITE);
    }
}

static void* find_pgaddr(int vpn) {
    return (char*)vm_begin + (vpn * pgsize);
}
