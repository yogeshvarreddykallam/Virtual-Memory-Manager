#include "vmm.h"
#include <unistd.h>

/* --- Define Global State Variables --- */
// (Declared as extern in vmm.h)
enum policy_type g_policy;
void *g_vm_start;
int g_num_pages;
int g_num_frames;
int g_page_size;
struct mm_stats *g_stats;

PageTableEntry *g_page_table;
FrameTableEntry *g_frames;
int g_frames_used;

int *g_fifo_queue = NULL;
int g_fifo_head = 0;
int g_fifo_tail = 0;
int g_clock_hand = 0;

struct sigaction g_old_sa;


/**
 * @brief Initializes the memory manager.
 */
void mm_init(enum policy_type policy, void *vm, int vm_size,
             int num_frames, int page_size, struct mm_stats *stats) {
    
    // 1. Store all global configuration
    g_policy = policy;
    g_vm_start = vm;
    g_num_frames = num_frames;
    g_page_size = page_size;
    g_stats = stats;
    g_num_pages = vm_size / page_size;
    g_frames_used = 0;

    // 2. Allocate Page Table
    g_page_table = (PageTableEntry *)malloc(sizeof(PageTableEntry) * g_num_pages);
    for (int i = 0; i < g_num_pages; i++) {
        g_page_table[i].frame_index = -1;
        g_page_table[i].present = 0;
        g_page_table[i].reference_bit = 0;
        g_page_table[i].modified_bit = 0;
    }

    // 3. Allocate Frame Table
    g_frames = (FrameTableEntry *)malloc(sizeof(FrameTableEntry) * g_num_frames);
    for (int i = 0; i < g_num_frames; i++) {
        g_frames[i].vpn = -1;
    }

    // 4. Initialize policy-specific structures
    if (g_policy == MM_FIFO) {
        g_fifo_queue = (int *)malloc(sizeof(int) * g_num_frames);
        g_fifo_head = 0;
        g_fifo_tail = 0;
    } else if (g_policy == MM_THIRD) {
        g_clock_hand = 0;
    }

    // 5. Protect the entire virtual memory region
    // This ensures any access will fault, letting our handler take over.
    if (mprotect(g_vm_start, vm_size, PROT_NONE) == -1) {
        perror("mprotect init failed");
        exit(EXIT_FAILURE);
    }

    // 6. Register the SIGSEGV signal handler
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;         // Use sa_sigaction, not sa_handler
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signal_handler; // Our handler function in vmm.c
    
    // Save the old handler to restore it on finish
    if (sigaction(SIGSEGV, &sa, &g_old_sa) == -1) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Cleans up the memory manager.
 */
void mm_finish() {
    // 1. Un-protect the memory region
    mprotect(g_vm_start, g_num_pages * g_page_size, PROT_READ | PROT_WRITE);

    // 2. Restore the original SIGSEGV handler
    sigaction(SIGSEGV, &g_old_sa, NULL);

    // 3. Free all allocated metadata
    free(g_page_table);
    free(g_frames);
    if (g_policy == MM_FIFO) {
        free(g_fifo_queue);
    }
}