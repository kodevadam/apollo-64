#ifndef APOLLO64_SOUND_H
#define APOLLO64_SOUND_H

#include <stdbool.h>

/*
 * apollo-64 sound. All waveforms are generated procedurally - no audio
 * assets - so the ROM stays self-contained and deterministic.
 *
 *   - a short mechanical "click" on every DSKY keystroke
 *   - a steady caution tone while the AGC is asserting an operator-error
 *     condition (the master-alarm sound; silenced when the error clears)
 */

void sound_init(void);

/* Trigger one DSKY key click. Fire-and-forget; safe to call every time a
 * keystroke is registered. */
void sound_key_click(void);

/* Turn the caution tone on or off. Driven by the AGC's OPR ERR state, so
 * it sounds exactly while the real error condition is asserted. */
void sound_set_alarm(bool on);

/* Fill any free audio buffers. Call once per video frame. */
void sound_update(void);

#endif
