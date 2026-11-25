#include <stdint.h>

// Syscall numbers, synced with src/cpu/interrupt.c
#define SYS_PUT_CHAR_AT 14
#define SYS_BEEP        18
#define SYS_GET_TICKS   19

// Status ditulis di baris bawah, sebelah kiri jam (clock memakai kolom 72-79)
#define STATUS_ROW 24
#define STATUS_COL 56
#define STATUS_LEN 14

// Jeda hening antar nada supaya dua nada sama tetap terdengar terpisah
#define NOTE_GAP_MS 40
#define BEAT_MS     400

struct Note {
    const char *name;
    uint32_t    freq;
    uint32_t    beats;
};

// Frekuensi oktaf 4 (A4 = 440 Hz), dibulatkan ke Hz terdekat
#define C4 { "C4", 262, 1 }
#define D4 { "D4", 294, 1 }
#define E4 { "E4", 330, 1 }
#define F4 { "F4", 349, 1 }
#define G4 { "G4", 392, 1 }
#define A4 { "A4", 440, 1 }
#define B4 { "B4", 494, 1 }
#define C5 { "C5", 523, 1 }
#define G4_LONG { "G4", 392, 2 }
#define C4_LONG { "C4", 262, 2 }
#define D4_LONG { "D4", 294, 2 }
#define C5_LONG { "C5", 523, 2 }
#define A4_LONG   { "A4",  440, 2 }
#define B4_LONG   { "B4",  494, 2 }
#define CS5       { "C#5", 554, 1 }
#define CS5_LONG  { "C#5", 554, 2 }
#define D5        { "D5",  587, 1 }
#define D5_LONG   { "D5",  587, 2 }
#define D5_HOLD   { "D5",  587, 4 }
#define E5        { "E5",  659, 1 }
#define E5_LONG   { "E5",  659, 2 }
#define E5_HOLD   { "E5",  659, 4 }
#define FS5       { "F#5", 740, 1 }
#define FS5_LONG  { "F#5", 740, 2 }
#define A5        { "A5",  880, 1 }
#define A5_LONG   { "A5",  880, 2 }

static const struct Note song[] = {
    C4, D4, E4, F4, G4, A4, B4, C5_LONG,
    C4, C4, G4, G4, A4, A4, G4_LONG,
    F4, F4, E4, E4, D4, D4, C4_LONG,
    G4, G4, F4, F4, E4, E4, D4_LONG,
    G4, G4, F4, F4, E4, E4, D4_LONG,
    C4, C4, G4, G4, A4, A4, G4_LONG,
    F4, F4, E4, E4, D4, D4, C4_LONG,
};

/* static const struct Note song[] = {
    A4, B4, D5, B4, FS5, FS5, E5_LONG,
    A4, B4, D5, B4, E5, E5, D5, CS5, B4_LONG,
    A4, B4, D5, B4, D5, E5, CS5, A4, A4, E5, D5_LONG,
    A4, B4, D5, B4, FS5, FS5, E5_LONG,
    A4, B4, D5, B4, A5, CS5, D5, CS5, B4_LONG,
    A4, B4, D5, B4, D5, E5, CS5, A4, A4, E5, D5_LONG,
}; */

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile(
        "movl %0, %%eax\n\t"
        "int $0x30"
        :
        : "rm"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "eax", "memory"
    );
}

static uint32_t now_ms(void) {
    uint32_t ticks = 0;
    syscall(SYS_GET_TICKS, (uint32_t) &ticks, 0, 0);
    return ticks;
}

static void wait_ms(uint32_t ms) {
    uint32_t end = now_ms() + ms;
    while ((int32_t) (now_ms() - end) < 0)
        ;
}

static void show_status(const char *text) {
    uint32_t i = 0;
    for (; i < STATUS_LEN && text[i] != '\0'; i++)
        syscall(SYS_PUT_CHAR_AT, (uint32_t) text[i], STATUS_ROW, STATUS_COL + i);
    for (; i < STATUS_LEN; i++)
        syscall(SYS_PUT_CHAR_AT, (uint32_t) ' ', STATUS_ROW, STATUS_COL + i);
}

static void show_note(const struct Note *note) { 
    char text[STATUS_LEN + 1] = "Music: ";
    uint32_t len = 7;
    text[len++] = note->name[0];
    text[len++] = note->name[1];
    text[len++] = ' ';
    text[len++] = (char) ('0' + note->freq / 100 % 10);
    text[len++] = (char) ('0' + note->freq / 10 % 10);
    text[len++] = (char) ('0' + note->freq % 10);
    text[len] = '\0';
    show_status(text);
}

int main(void) {
    for (uint32_t i = 0; i < sizeof(song) / sizeof(song[0]); i++) {
        uint32_t duration = song[i].beats * BEAT_MS;
        show_note(&song[i]);
        // Kernel mematikan nada sendiri setelah durasi habis, user cukup menunggu
        syscall(SYS_BEEP, song[i].freq, duration - NOTE_GAP_MS, 0);
        wait_ms(duration);
    }
    show_status("");
    return 0;
}
