#ifndef _SPEAKER_H
#define _SPEAKER_H

#include <stdint.h>

/**
 * Mainkan nada lewat PC Speaker (PIT channel 2), non-blocking.
 * Nada dimatikan otomatis oleh speaker_update() setelah duration_ms.
 *
 * @param freq        Frekuensi nada dalam Hz (19..20000), di luar rentang atau 0 = stop
 * @param duration_ms Lama nada dalam milidetik, 0 = stop
 * @param now_ms      Waktu sekarang dalam tick timer (1 tick = 1 ms)
 */
void speaker_beep(uint32_t freq, uint32_t duration_ms, uint32_t now_ms);

/**
 * Matikan PC Speaker sekarang juga.
 */
void speaker_stop(void);

/**
 * Dipanggil dari IRQ0 setiap tick, mematikan nada yang waktunya sudah habis.
 *
 * @param now_ms Waktu sekarang dalam tick timer
 */
void speaker_update(uint32_t now_ms);

#endif
