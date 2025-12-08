#include "vmm.h"

enum policy_type type;
void *vm_begin;
int num_of_pages;
int num_of_fr;
int pgsize;
struct mm_stats *stats;
PageTableEntry *pt;
FrameTableEntry *fr;
int fr_used;
int *queue = NULL;
int head = 0;
int tail = 0;
int clk = 0;
struct sigaction old_sa;

void mm_init(enum policy_type policy, void *vm, int vm_size,
             int num_frames, int page_size, struct mm_stats *stats_in) {
    type = policy;
    vm_begin = vm;
    num_of_fr = num_frames;
    pgsize = page_size;
    stats = stats_in;
    num_of_pages = vm_size / page_size;
    fr_used = 0;

    // Allocation of PT and Frame Table
    pt = (PageTableEntry *)malloc(sizeof(PageTableEntry) * num_of_pages);
    for (int i = 0; i < num_of_pages; i++) {
        pt[i].frame_no = -1;
        pt[i].present = 0;
        pt[i].ref_bit = 0;
        pt[i].mod_bit = 0;
    }

    fr = (FrameTableEntry *)malloc(sizeof(FrameTableEntry) * num_of_fr);
    for (int i = 0; i < num_of_fr; i++) {
        fr[i].vpn = -1;
    }

    if (type == MM_FIFO) {
        queue = (int *)malloc(sizeof(int) * num_of_fr);
        head = 0;
        tail = 0;
    } else if (type == MM_THIRD) {
        clk = 0;
    }

    // Protect the entire virtual memory range initially
    if (mprotect(vm_begin, vm_size, PROT_NONE) == -1) {
        perror("mprotect init failed");
        exit(EXIT_FAILURE);
    }

    // Set up the signal handler
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signal_handler;

    // Save the old handler and install the new one
    if (sigaction(SIGSEGV, &sa, &old_sa) == -1) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }
}

void mm_finish() {
    // Restore permissions and signal handler
    mprotect(vm_begin, num_of_pages * pgsize, PROT_READ | PROT_WRITE);
    sigaction(SIGSEGV, &old_sa, NULL);

    // Free allocated memory
    free(pt);
    free(fr);
    if (type == MM_FIFO && queue != NULL) {
        free(queue);
    }
}
