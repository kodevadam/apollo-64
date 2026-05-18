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

  while (1) {
    /* Step the AGC for one frame's worth of cycles. */
    agc_host_tick(AGC_CYCLES_PER_FRAME);

    /* Poll input and redraw. */
    input_poll();

    surface_t *fb = display_get();
    dsky_render(fb);
    display_show(fb);
  }
}
