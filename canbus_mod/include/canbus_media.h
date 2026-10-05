#ifndef CANBUS_MEDIA_H
#define CANBUS_MEDIA_H

#include <stdint.h>
#include "canbus_slcan.h"

enum canbus_media_key {
	CANBUS_MEDIA_NONE,
	CANBUS_MEDIA_NEXT,
	CANBUS_MEDIA_PREVIOUS,
	CANBUS_MEDIA_PLAY,
	CANBUS_MEDIA_STOP
};

struct canbus_media_state {
	enum canbus_media_key held;
	uint32_t first_tick;
	uint32_t last_tick;
	uint8_t long_sent;
};

/* Short presses resolve on release or gap; long presses fire once while held. */
enum canbus_media_key canbus_media_apply(struct canbus_media_state *state,
		const struct canbus_frame *frame, uint32_t tick,
		uint32_t release_gap_ticks, uint32_t long_press_ticks);

/* End a press if its release frame was lost. Call periodically while idle. */
enum canbus_media_key canbus_media_expire(struct canbus_media_state *state,
		uint32_t tick, uint32_t release_gap_ticks);

#endif
