/*
 * apollo-64: entry point.
 *
 * The AGC is paced to real time: each iteration of the main loop measures
 * how much wall-clock time actually elapsed and runs exactly that many AGC
 * machine cycles. One real second therefore equals AGC_PER_SECOND engine
 * cycles regardless of video frame rate or momentary stalls.
 *
 * Pacing this way - rather than from a high-rate timer interrupt - is
 * deliberate. The AGC engine has no wall-clock reference of its own; its
 * internal timers (TIME1-6, the scaler) advance purely on the cycle
 * counter. Running a frame's worth of cycles in a burst is therefore
 * indistinguishable, from the AGC software's point of view, from running
 * them smoothly - the interrupts still fire in the right order at the
 * right relative cycle counts. Catch-up in the main loop also avoids any
 * reentrancy hazard between an ISR and the renderer.
 */

#include <libdragon.h>

#include "agc_host.h"
#include "dsky.h"
#include "input.h"
#include "rope.h"
#include "sound.h"

/* Don't let a long stall (debugger break, first frame) trigger a huge
 * catch-up burst - cap a single step at a quarter-second of AGC time. */
#define AGC_CATCHUP_CAP (AGC_PER_SECOND / 4)

/* Power-on sequence, run automatically at boot:
 *
 *   RSET                     clear the power-up RESTART light
 *   VERB 1 6 NOUN 3 6 ENTR   V16N36E - monitor the AGC mission clock
 *
 * This leaves the panel showing a live, ticking time so the display is
 * visibly driven by real AGC state from the moment it boots. The operator
 * can then key anything else (V35E lamp test, etc.) via the controller.
 *
 * Keypad codes (channel 015): see src/input.h. */
static const uint8_t poweron_seq[] = {
  022,                       /* RSET */
  021, 001, 006,             /* V16  */
  037, 003, 006, 034,        /* N36E - monitor AGC clock */
};
#define POWERON_STEPS  (sizeof poweron_seq / sizeof poweron_seq[0])
#define POWERON_SPACING 45   /* main-loop iterations between checkout keys */

int
main(void)
{
  display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
  timer_init();              /* needed for the 64-bit get_ticks_us() clock */
  debug_init_isviewer();
  debug_init_usblog();

  input_init();
  dsky_init();
  sound_init();
  agc_host_init();

  debugf("apollo-64: AGC initialised, CoreRope @ %p, entry Z=%04o\n",
         (void *)CoreRope, g_agc.Erasable[0][RegZ]);

  unsigned long iter = 0;
  unsigned poweron_step = 0;

  /* Real-time pacing state. cycle_accum carries the sub-microsecond
   * remainder so the average rate is exact over time. */
  uint64_t last_us = get_ticks_us();
  uint64_t cycle_accum = 0;

  while (1) {
    /* Measure elapsed wall time and convert to AGC cycles:
     *   cycles = elapsed_us * AGC_PER_SECOND / 1e6
     * keeping the fractional part in cycle_accum. */
    uint64_t now_us = get_ticks_us();
    uint64_t elapsed_us = now_us - last_us;
    last_us = now_us;

    cycle_accum += elapsed_us * (uint64_t)AGC_PER_SECOND;
    uint32_t cycles = (uint32_t)(cycle_accum / 1000000u);
    cycle_accum %= 1000000u;
    if (cycles > AGC_CATCHUP_CAP) cycles = AGC_CATCHUP_CAP;

    agc_host_tick(cycles);

    /* Power-on checkout: feed the V16N36E sequence over the first ~2 s,
     * one keystroke every POWERON_SPACING iterations, starting after a
     * short settle. */
    if (poweron_step < POWERON_STEPS &&
        iter >= 60 + (unsigned long)POWERON_SPACING * poweron_step) {
      agc_host_press_key(poweron_seq[poweron_step]);
      sound_key_click();   /* the auto checkout keys click too */
      poweron_step++;
    }

    input_poll();

    surface_t *fb = display_get();
    dsky_render(fb);
    display_show(fb);
    sound_update();
    iter++;
  }
}
