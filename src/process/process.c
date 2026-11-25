#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};

static struct {
    uint32_t active_process_count;
    uint32_t last_pid;
    uint32_t next_start_seq;
} process_manager_state = {
    .active_process_count = 0,
    .last_pid             = 0,
    .next_start_seq       = 1,
};

static uint32_t ceil_div(uint32_t numerator, uint32_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (!_process_list[i].metadata.active) {
            return i;
        }
    }
    return -1;
}

static uint32_t process_generate_new_pid(void) {
    uint32_t pid = process_manager_state.last_pid;
    process_manager_state.last_pid++;
    return pid;
}

static int32_t process_list_get_index_by_pid(uint32_t pid) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.active && _process_list[i].metadata.pid == pid) {
            return i;
        }
    }
    return -1;
}

// Instance lama dari executable yang sama tidak dijalankan bersamaan.
// Yang sedang RUNNING ditandai pending agar context-nya sempat disimpan.
static void process_suspend_older_instances(uint32_t program_inode, uint32_t new_pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (!pcb->metadata.active || pcb->metadata.pid == new_pid)
            continue;
        if (pcb->metadata.program_inode != program_inode)
            continue;

        if (pcb->metadata.state == READY) {
            pcb->metadata.state = WAITING;
            pcb->metadata.suspended = true;
        } else if (pcb->metadata.state == RUNNING) {
            pcb->metadata.suspend_pending = true;
        }
    }
}

// Saat instance terbaru berhenti, aktifkan kembali instance lama terbaru
// dari executable yang sama.
static void process_resume_older_instance(uint32_t program_inode) {
    bool has_current_instance = false;
    int32_t newest_suspended = -1;
    uint32_t newest_seq = 0;

    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (!pcb->metadata.active || pcb->metadata.program_inode != program_inode)
            continue;

        if (!pcb->metadata.suspended) {
            has_current_instance = true;
        } else if (pcb->metadata.state == WAITING && pcb->metadata.start_seq >= newest_seq) {
            newest_seq = pcb->metadata.start_seq;
            newest_suspended = (int32_t) i;
        }
    }

    if (has_current_instance || newest_suspended < 0)
        return;

    struct ProcessControlBlock *pcb = &_process_list[newest_suspended];
    pcb->metadata.suspended = false;
    pcb->metadata.suspend_pending = false;
    pcb->metadata.state = READY;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.active && _process_list[i].metadata.state == RUNNING) {
            return &_process_list[i];
        }
    }
    return NULL;
}

// Alokasikan satu halaman user di page directory yang SEDANG AKTIF, lalu catat ke PCB
static bool process_allocate_page(struct ProcessControlBlock *pcb, struct PageDirectory *page_dir, void *virtual_addr) {
    if (pcb->memory.page_frame_used_count >= PROCESS_PAGE_FRAME_COUNT_MAX) {
        return false;
    }
    if (!paging_allocate_user_page_frame(page_dir, virtual_addr)) {
        return false;
    }
    pcb->memory.virtual_addr_used[pcb->memory.page_frame_used_count] = virtual_addr;
    pcb->memory.page_frame_used_count++;
    return true;
}

// Bebaskan semua halaman user milik pcb. page_dir milik pcb harus sedang aktif.
static void process_release_pages(struct ProcessControlBlock *pcb, struct PageDirectory *page_dir) {
    for (uint32_t i = 0; i < pcb->memory.page_frame_used_count; i++) {
        paging_free_user_page_frame(page_dir, pcb->memory.virtual_addr_used[i]);
        pcb->memory.virtual_addr_used[i] = NULL;
    }
    pcb->memory.page_frame_used_count = 0;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;
    struct PageDirectory *old_page_dir = paging_get_current_page_directory_addr();
    struct PageDirectory *new_page_dir = NULL;
    struct ProcessControlBlock *new_pcb = NULL;

    if (request.name == NULL || request.name_len == 0)
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;

    uint32_t program_inode = find_file_entry(request.parent_inode, request.name, request.name_len);
    if (program_inode == 0)
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;

    // 0. Validasi
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    // Entrypoint tidak boleh berada di area kernel (higher half)
    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    // Kebutuhan memori: area program tetap (bss tidak ada di flat binary) + stack + 1 frame page table
    uint32_t page_frame_count_needed = PROCESS_USER_PROGRAM_PAGES + PROCESS_USER_STACK_PAGES;
    if (page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX
        || ceil_div(request.buffer_size, PAGE_FRAME_SIZE) > PROCESS_USER_PROGRAM_PAGES
        || !paging_allocate_check((page_frame_count_needed + 1) * PAGE_FRAME_SIZE)) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }
    new_pcb = &_process_list[p_index];
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

    // 1. Virtual address space baru
    new_page_dir = paging_create_new_page_directory();
    if (new_page_dir == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // paging_allocate_user_page_frame() hanya bisa bekerja pada page directory yang aktif (recursive mapping)
    paging_use_page_directory(new_page_dir);

    for (uint32_t i = 0; i < PROCESS_USER_PROGRAM_PAGES; i++) {
        void *virtual_addr = (void *) ((uint32_t) request.buf + i * PAGE_FRAME_SIZE);
        if (!process_allocate_page(new_pcb, new_page_dir, virtual_addr)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
    }
    for (uint32_t i = 1; i <= PROCESS_USER_STACK_PAGES; i++) {
        void *virtual_addr = (void *) (PROCESS_USER_STACK_TOP - i * PAGE_FRAME_SIZE);
        if (!process_allocate_page(new_pcb, new_page_dir, virtual_addr)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
    }
    // Frame bisa berisi sisa data process lain, nol-kan supaya .bss bersih
    memset(request.buf, 0, PROCESS_USER_PROGRAM_SIZE);

    // 2. Load executable dari file system ke memory process baru
    request.buffer_size = PROCESS_USER_PROGRAM_SIZE;
    int8_t read_status = read(request);
    if (read_status != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    // 3. Context awal
    uint32_t user_data_selector = 0x20 | 0x3;
    new_pcb->context.cpu.segment.ds = user_data_selector;
    new_pcb->context.cpu.segment.es = user_data_selector;
    new_pcb->context.cpu.segment.fs = user_data_selector;
    new_pcb->context.cpu.segment.gs = user_data_selector;
    new_pcb->context.cpu.stack.esp  = PROCESS_USER_STACK_TOP - 4;
    new_pcb->context.cpu.stack.ebp  = PROCESS_USER_STACK_TOP - 4;
    new_pcb->context.eip            = (uint32_t) request.buf;
    new_pcb->context.eflags         = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = new_page_dir;

    // 4. Metadata
    uint32_t name_len = request.name_len;
    if (name_len >= PROCESS_NAME_LENGTH_MAX) {
        name_len = PROCESS_NAME_LENGTH_MAX - 1;
    }
    memcpy(new_pcb->metadata.name, request.name, name_len);
    new_pcb->metadata.name[name_len] = '\0';
    new_pcb->metadata.pid    = process_generate_new_pid();
    new_pcb->metadata.state  = READY;
    new_pcb->metadata.active = true;
    new_pcb->metadata.program_inode = program_inode;
    new_pcb->metadata.start_seq = process_manager_state.next_start_seq++;
    process_manager_state.active_process_count++;

    // Boleh ada banyak instance, tetapi instance lama dari program yang sama
    // ditahan sampai instance terbaru berhenti.
    process_suspend_older_instances(program_inode, new_pcb->metadata.pid);

exit_cleanup:
    // Rollback kalau gagal setelah address space dibuat
    if (retcode != PROCESS_CREATE_SUCCESS && new_page_dir != NULL) {
        paging_use_page_directory(new_page_dir);
        process_release_pages(new_pcb, new_page_dir);
        paging_use_page_directory(old_page_dir);
        paging_free_page_directory(new_page_dir);
        memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
    }
    // 5. Kembalikan virtual address space seperti sebelum fungsi dipanggil
    paging_use_page_directory(old_page_dir);
    return retcode;
}

bool process_destroy(uint32_t pid) {
    int32_t index = process_list_get_index_by_pid(pid);
    if (index < 0) {
        return false;
    }
    struct ProcessControlBlock *pcb = &_process_list[index];
    struct PageDirectory *page_dir = pcb->context.page_directory_virtual_addr;
    uint32_t program_inode = pcb->metadata.program_inode;
    struct PageDirectory *old_page_dir = paging_get_current_page_directory_addr();

    // Frame user hanya bisa dibebaskan saat page directory miliknya aktif
    paging_use_page_directory(page_dir);
    process_release_pages(pcb, page_dir);

    // Kalau yang dihancurkan adalah process yang sedang aktif (mis. exit), pindah ke page directory kernel
    if (old_page_dir == page_dir) {
        paging_use_page_directory(&_paging_kernel_page_directory);
    } else {
        paging_use_page_directory(old_page_dir);
    }
    paging_free_page_directory(page_dir);

    memset(pcb, 0, sizeof(struct ProcessControlBlock));
    process_manager_state.active_process_count--;

    // Instance terbaru sudah berhenti; instance lama terbaru boleh lanjut.
    process_resume_older_instance(program_inode);
    return true;
}
