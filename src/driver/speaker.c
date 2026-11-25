#include <stdbool.h>
#include "header/driver/speaker.h"
#include "header/cpu/portio.h"

#define PIT_BASE_FREQUENCY        1193182
#define PIT_COMMAND_PORT          0x43
#define PIT_CHANNEL_2_DATA_PORT   0x42
// Channel 2, akses lobyte/hibyte, mode 3 (square wave), binary
#define PIT_CHANNEL_2_SQUARE_WAVE 0xB6
#define SPEAKER_CONTROL_PORT      0x61
// Bit 0: gate PIT channel 2, bit 1: sambungkan output PIT ke speaker
#define SPEAKER_ENABLE_BITS       0x03
// Divisor PIT 16-bit, jadi frekuensi minimum 1193182 / 65535 ~ 19 Hz
#define SPEAKER_MIN_FREQUENCY     19
#define SPEAKER_MAX_FREQUENCY     20000

static bool     speaker_playing = false;
static uint32_t speaker_stop_at = 0;

void speaker_stop(void) {
    out(SPEAKER_CONTROL_PORT, in(SPEAKER_CONTROL_PORT) & ~SPEAKER_ENABLE_BITS);
    speaker_playing = false;
}

void speaker_beep(uint32_t freq, uint32_t duration_ms, uint32_t now_ms) {
    if (freq < SPEAKER_MIN_FREQUENCY || freq > SPEAKER_MAX_FREQUENCY || duration_ms == 0) {
        speaker_stop();
        return;
    }

    uint16_t divisor = (uint16_t) (PIT_BASE_FREQUENCY / freq);
    out(PIT_COMMAND_PORT, PIT_CHANNEL_2_SQUARE_WAVE);
    out(PIT_CHANNEL_2_DATA_PORT, (uint8_t) (divisor & 0xFF));
    out(PIT_CHANNEL_2_DATA_PORT, (uint8_t) (divisor >> 8));
    out(SPEAKER_CONTROL_PORT, in(SPEAKER_CONTROL_PORT) | SPEAKER_ENABLE_BITS);

    speaker_stop_at = now_ms + duration_ms;
    speaker_playing = true;
}

void speaker_update(uint32_t now_ms) {
    // Selisih signed supaya tetap benar saat counter tick overflow
    if (speaker_playing && (int32_t) (now_ms - speaker_stop_at) >= 0)
        speaker_stop();
}
