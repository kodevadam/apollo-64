#ifndef APOLLO64_INPUT_H
#define APOLLO64_INPUT_H

#include <stdint.h>

/* DSKY keypad codes (5-bit values written to channel 015 bits 4:0).
 * Values cross-checked against yaDSKY2 source and AGC documentation. */
#define DSKY_KEY_NONE     000
#define DSKY_KEY_1        001
#define DSKY_KEY_2        002
#define DSKY_KEY_3        003
#define DSKY_KEY_4        004
#define DSKY_KEY_5        005
#define DSKY_KEY_6        006
#define DSKY_KEY_7        007
#define DSKY_KEY_8        010
#define DSKY_KEY_9        011
#define DSKY_KEY_0        020
#define DSKY_KEY_VERB     021
#define DSKY_KEY_RSET     022
#define DSKY_KEY_KEY_REL  031
#define DSKY_KEY_PLUS     032
#define DSKY_KEY_MINUS    033
#define DSKY_KEY_ENTR     034
#define DSKY_KEY_CLR      036
#define DSKY_KEY_NOUN     037
#define DSKY_KEY_PRO      0177  /* PROCEED uses a different channel (032 bit 14); */
                                /* sentinel handled specially in input.c. */

void input_init(void);

/* Poll the controller and translate any new keypress into a DSKY key code,
 * which is delivered via agc_host_press_key(). Designed to be called once per
 * video frame; debounces against the previous frame's button state. */
void input_poll(void);

#endif
