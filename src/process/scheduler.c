#include "header/process/scheduler.h"
#include "header/cpu/interrupt.h"

// refers to index process dari process list untuk kebutuhan scheduling. -1 artinya blm ada yang dimulai
static int32_t current_process_schedule_index;

void scheduler_init(void) {
    // blm kepikiran lagi mau set up apa
    current_process_schedule_index = -1;
    activate_timer_interrupt();
}

// terkait pengurutan apakah yang diduluin itu yang baru masuk atau yang baru selesai itu tergantung posisinya terhadap current_process_schedule index. technically masih termasuk round robin karena preemptive-nya berdasarkan time slice matters
static int32_t next_queue(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        // cari secara sirkuler
        int32_t base = (current_process_schedule_index == -1) ? 0 : current_process_schedule_index;
        int32_t idx = (base + (i + 1)) % PROCESS_COUNT_MAX;
        if (_process_list[idx].metadata.active && _process_list[idx].metadata.state == READY) {
            return idx;
        }
    }
    return -1;
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    if (current_process_schedule_index == -1) return;

    struct ProcessControlBlock *pcb = &_process_list[current_process_schedule_index];
    if (!pcb->metadata.active || pcb->metadata.state != RUNNING) return;

    pcb->context = ctx;

    // Instance lama dari program yang sama disimpan sampai instance baru selesai.
    if (pcb->metadata.suspend_pending) {
        pcb->metadata.suspend_pending = false;
        pcb->metadata.suspended = true;
        pcb->metadata.state = WAITING;
    }
}

void scheduler_switch_to_next_process(void) {
    int32_t next = next_queue();
    if (next == -1) {
        // Tidak ada process READY lain: lanjutkan process yang sedang RUNNING
        bool current_valid = current_process_schedule_index != -1
            && _process_list[current_process_schedule_index].metadata.active
            && _process_list[current_process_schedule_index].metadata.state == RUNNING;
        if (!current_valid) {
            // Tidak ada process sama sekali, berhenti di sini
            while (true) {
                __asm__ volatile("hlt");
            }
        }
        next = current_process_schedule_index;
    }

    if (current_process_schedule_index != -1) {
        struct ProcessControlBlock *current = &_process_list[current_process_schedule_index];
        if (current->metadata.active && current->metadata.state == RUNNING)
            current->metadata.state = READY;
    }
    current_process_schedule_index = next;
    _process_list[next].metadata.state = RUNNING;
    paging_use_page_directory(_process_list[next].context.page_directory_virtual_addr);
    process_context_switch(_process_list[next].context);
}