#ifndef APOLLO64_DSKY_H
#define APOLLO64_DSKY_H

#include <libdragon.h>

void dsky_init(void);

/* Render the current DSKY state into the supplied display surface. Pulls from
 * g_dsky (updated by agc_host.c::ChannelOutput). Call once per video frame. */
void dsky_render(surface_t *fb);

#endif
