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
int32_t next_queue(void) {
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

void scheduler_save_context_to_current_running_pcb(struct Context ctx);

void scheduler_switch_to_next_process(void) {
    int32_t next = next_queue();
    if (next == -1) return;

    if (current_process_schedule_index != -1) {
        _process_list[current_process_schedule_index].metadata.state = READY;
    }
    current_process_schedule_index = next;
    _process_list[next].metadata.state = RUNNING;
    paging_use_page_directory(_process_list[next].context.page_directory_virtual_addr);
    process_context_switch(_process_list[next].context);
}