/*
 * apollo-64: entry point.
 *
 * Top-level loop:
 *   1. Init video, input, AGC.
 *   2. Once per vblank, render the DSKY and poll the controller.
 *   3. Between vblanks, run the AGC engine flat-out. (Real-time pacing
 *      via a timer ISR is a TODO; see comment near agc_host_tick.)
 *
 * Real AGC ran at 1024 kHz / 12 = ~85.3 kHz machine-cycle rate. The N64 is
 * fast enough to run the engine well above real-time, so we batch a tick
 * budget per frame instead of paying interrupt overhead.
 */

#include <libdragon.h>

#include "agc_host.h"
#include "dsky.h"
#include "input.h"
#include "rope.h"

/* AGC cycles per video frame at 60 Hz, targeting real-time fidelity:
 *   AGC_PER_SECOND (from agc_engine.h) / 60 ~= 1422 cycles/frame.
 * agc_engine() is one machine cycle; on a 93.75 MHz R4300i this is well
 * under a millisecond per frame even pessimistically. */
#define AGC_CYCLES_PER_FRAME ((1024000 / 12) / 60)

/* Power-on sequence, run automatically at boot:
 *
 *   RSET                     clear the power-up RESTART light
 *   VERB 1 6 NOUN 3 6 ENTR   V16N36E - monitor the AGC mission clock
 *
 * This leaves the panel showing a live, ticking time so the display
 * is visibly driven by real AGC state from the moment it boots. The
 * operator can then key anything else (V35E lamp test, etc.) via the
 * controller. We avoid auto-running the lamp test because V35E holds
 * DSPLOCK for 5 s, which would swallow the keystrokes that follow it.
 *
 * Keypad codes (channel 015): see src/input.h. */
static const uint8_t poweron_seq[] = {
  022,                       /* RSET */
  021, 001, 006,             /* V16  */
  037, 003, 006, 034,        /* N36E - monitor AGC clock */
};
#define POWERON_STEPS  (sizeof poweron_seq / sizeof poweron_seq[0])
#define POWERON_SPACING 45   /* video frames between checkout keystrokes */

int
main(void)
{
  /* libdragon basics. */
  display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
  debug_init_isviewer();
  debug_init_usblog();

  input_init();
  dsky_init();
  agc_host_init();

  debugf("apollo-64: AGC initialised, CoreRope @ %p, entry Z=%04o\n",
         (void *)CoreRope, g_agc.Erasable[0][RegZ]);

  unsigned long frame = 0;
  unsigned poweron_step = 0;

  while (1) {
    /* Step the AGC for one frame's worth of cycles. */
    agc_host_tick(AGC_CYCLES_PER_FRAME);

    /* Power-on checkout: feed the V35E lamp-test sequence over the first
     * couple of seconds, then stop and let the operator drive. The first
     * keystroke waits ~1s (60 frames) so the executive has settled. */
    if (poweron_step < POWERON_STEPS &&
        frame >= 60 + (unsigned long)POWERON_SPACING * poweron_step) {
      agc_host_press_key(poweron_seq[poweron_step]);
      poweron_step++;
    }

    /* Poll input and redraw. */
    input_poll();

    surface_t *fb = display_get();
    dsky_render(fb);
    display_show(fb);
    frame++;
  }
}
