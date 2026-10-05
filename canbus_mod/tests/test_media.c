#include <assert.h>
#include <stdint.h>
#include "canbus_media.h"

#define GAP 50
#define LONG 100

static enum canbus_media_key send(struct canbus_media_state *state,
		struct canbus_frame *frame, uint8_t button, uint32_t tick)
{
	frame->data[0] = button;
	return canbus_media_apply(state, frame, tick, GAP, LONG);
}

int main(void)
{
	struct canbus_media_state state = {0};
	struct canbus_frame frame = {0x1d6, 2, {0xe0, 0x0c}};

	/* A short press resolves on release, without a long action. */
	assert(send(&state, &frame, 0xe0, 10) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xe0, 30) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xc0, 40) == CANBUS_MEDIA_NEXT);
	assert(send(&state, &frame, 0xc0, 41) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xd0, 50) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xc0, 60) == CANBUS_MEDIA_PREVIOUS);

	/* Up hold emits Play once; release does not send Next. */
	assert(send(&state, &frame, 0xe0, 100) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xe0, 149) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xe0, 199) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xe0, 200) == CANBUS_MEDIA_PLAY);
	assert(send(&state, &frame, 0xe0, 220) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xc0, 225) == CANBUS_MEDIA_NONE);

	/* Down hold sends Stop, including if only its release reports duration. */
	assert(send(&state, &frame, 0xd0, 300) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xd0, 350) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xd0, 400) == CANBUS_MEDIA_STOP);
	assert(send(&state, &frame, 0xc0, 410) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xd0, 500) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xc0, 600) == CANBUS_MEDIA_STOP);

	/* Missing release: gap resolves a short press and rearms the button. */
	frame.data[1] = 0;
	assert(send(&state, &frame, 0xe0, 700) == CANBUS_MEDIA_NONE);
	assert(canbus_media_expire(&state, 750, GAP) == CANBUS_MEDIA_NONE);
	assert(canbus_media_expire(&state, 751, GAP) == CANBUS_MEDIA_NEXT);
	assert(send(&state, &frame, 0xe0, 752) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xc0, 753) == CANBUS_MEDIA_NEXT);
	assert(send(&state, &frame, 0xe0, 760) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xe0, 811) == CANBUS_MEDIA_NEXT);
	assert(send(&state, &frame, 0xc0, 812) == CANBUS_MEDIA_NEXT);

	/* Other CAN IDs do not release a held button; a direction change does. */
	assert(send(&state, &frame, 0xe0, 820) == CANBUS_MEDIA_NONE);
	frame.id = 0x1c2;
	assert(send(&state, &frame, 0xc0, 830) == CANBUS_MEDIA_NONE);
	frame.id = 0x1d6;
	assert(send(&state, &frame, 0xd0, 840) == CANBUS_MEDIA_NEXT);
	assert(send(&state, &frame, 0xc0, 850) == CANBUS_MEDIA_PREVIOUS);

	/* Unsigned tick subtraction survives a clock wrap. */
	assert(send(&state, &frame, 0xe0, UINT32_MAX - 41) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xe0, 8) == CANBUS_MEDIA_NONE);
	assert(send(&state, &frame, 0xe0, 58) == CANBUS_MEDIA_PLAY);
	assert(send(&state, &frame, 0xc0, 59) == CANBUS_MEDIA_NONE);
	return 0;
}
