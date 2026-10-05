#include "canbus_media.h"

static enum canbus_media_key finish_press(struct canbus_media_state *state)
{
	enum canbus_media_key result = CANBUS_MEDIA_NONE;

	if (state->held != CANBUS_MEDIA_NONE && !state->long_sent)
		result = state->held;
	state->held = CANBUS_MEDIA_NONE;
	state->long_sent = 0;
	return result;
}

enum canbus_media_key canbus_media_expire(struct canbus_media_state *state,
		uint32_t tick, uint32_t release_gap_ticks)
{
	if (state->held != CANBUS_MEDIA_NONE &&
	    tick - state->last_tick > release_gap_ticks)
		return finish_press(state);
	return CANBUS_MEDIA_NONE;
}

enum canbus_media_key canbus_media_apply(struct canbus_media_state *state,
		const struct canbus_frame *frame, uint32_t tick,
		uint32_t release_gap_ticks, uint32_t long_press_ticks)
{
	enum canbus_media_key key = CANBUS_MEDIA_NONE;
	enum canbus_media_key expired;

	if (frame->id != 0x1d6 || frame->dlc != 2)
		return canbus_media_expire(state, tick, release_gap_ticks);

	/* BMW E90 K-CAN: E0 = Up, D0 = Down.
	 * The trace uses 0C as the second byte; older captures use 00.
	 * Other values, including C0 release, end the held key. */
	if (frame->data[1] == 0 || frame->data[1] == 0x0c) {
		if (frame->data[0] == 0xe0)
			key = CANBUS_MEDIA_NEXT;
		else if (frame->data[0] == 0xd0)
			key = CANBUS_MEDIA_PREVIOUS;
	}
	if (key == CANBUS_MEDIA_NONE) {
		if (state->held != CANBUS_MEDIA_NONE && !state->long_sent &&
		    tick - state->first_tick >= long_press_ticks) {
			key = state->held == CANBUS_MEDIA_NEXT ? CANBUS_MEDIA_PLAY : CANBUS_MEDIA_STOP;
			state->held = CANBUS_MEDIA_NONE;
			state->long_sent = 0;
			return key;
		}
		return finish_press(state);
	}
	expired = canbus_media_expire(state, tick, release_gap_ticks);
	if (key != state->held) {
		enum canbus_media_key ended = finish_press(state);
		state->held = key;
		state->first_tick = tick;
		state->last_tick = tick;
		return expired != CANBUS_MEDIA_NONE ? expired : ended;
	}
	state->last_tick = tick;
	if (!state->long_sent && tick - state->first_tick >= long_press_ticks) {
		state->long_sent = 1;
		return key == CANBUS_MEDIA_NEXT ? CANBUS_MEDIA_PLAY : CANBUS_MEDIA_STOP;
	}
	return expired;
}
