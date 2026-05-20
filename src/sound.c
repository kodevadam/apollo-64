/*
 * apollo-64: procedural sound. libdragon-specific.
 *
 * Two voices, mixed into one mono signal duplicated to both channels:
 *
 *   click  - a ~25 ms square-wave burst (~2.2 kHz) with a linear decay
 *            envelope. Triggered per DSKY keystroke.
 *   alarm  - a steady ~900 Hz square tone, on while the AGC asserts
 *            OPR ERR. This stands in for the spacecraft master-alarm
 *            tone the crew would have heard.
 *
 * Samples are generated on the fly into the libdragon audio buffers; no
 * wav assets, nothing in the DFS.
 */

#include <libdragon.h>

#include "sound.h"

#define SAMPLE_RATE   22050
#define NUM_BUFFERS   4
#define CLICK_SAMPLES (SAMPLE_RATE / 40)   /* ~25 ms */

/* Square-wave half-periods, in samples. */
#define CLICK_HALF (SAMPLE_RATE / 2 / 2200)   /* ~2.2 kHz */
#define ALARM_HALF (SAMPLE_RATE / 2 / 900)    /* ~900 Hz  */

#define CLICK_AMP 9000
#define ALARM_AMP 6000

static bool     alarm_on;
static int      click_remaining;   /* samples left in the current click */
static uint32_t click_phase;
static uint32_t alarm_phase;

void
sound_init(void)
{
  audio_init(SAMPLE_RATE, NUM_BUFFERS);
  alarm_on = false;
  click_remaining = 0;
  click_phase = 0;
  alarm_phase = 0;
}

void
sound_key_click(void)
{
  click_remaining = CLICK_SAMPLES;
  click_phase = 0;
}

void
sound_set_alarm(bool on)
{
  alarm_on = on;
}

/* One mono sample: click (with decay envelope) plus alarm tone, summed
 * and clamped. */
static int16_t
next_sample(void)
{
  int32_t s = 0;

  if (click_remaining > 0) {
    int32_t env = (int32_t)CLICK_AMP * click_remaining / CLICK_SAMPLES;
    s += ((click_phase / CLICK_HALF) & 1) ? env : -env;
    click_phase++;
    click_remaining--;
  }

  if (alarm_on) {
    s += ((alarm_phase / ALARM_HALF) & 1) ? ALARM_AMP : -ALARM_AMP;
    alarm_phase++;
  } else {
    alarm_phase = 0;
  }

  if (s >  32767) s =  32767;
  if (s < -32768) s = -32768;
  return (int16_t)s;
}

void
sound_update(void)
{
  int len = audio_get_buffer_length();

  /* Fill every free audio buffer. audio_write_begin() hands back the
   * libdragon buffer directly (len stereo frames); we synthesise into it
   * and audio_write_end() submits it. */
  while (audio_can_write()) {
    short *buf = audio_write_begin();
    for (int i = 0; i < len; i++) {
      int16_t s = next_sample();
      buf[i * 2 + 0] = s;   /* left  */
      buf[i * 2 + 1] = s;   /* right */
    }
    audio_write_end();
  }
}
