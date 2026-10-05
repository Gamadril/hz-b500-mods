#ifndef CANBUS_RENDER_H
#define CANBUS_RENDER_H

#include "canbus_pdc.h"

int canbus_render_open(void);
void canbus_render_draw(const struct canbus_ui_state *state);
void canbus_render_close(void);

#endif
