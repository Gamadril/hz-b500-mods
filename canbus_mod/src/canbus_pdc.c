#include "canbus_pdc.h"

int canbus_pdc_apply(struct canbus_ui_state *state,
		     const struct canbus_frame *frame)
{
	static const uint8_t source[PDC_SENSOR_COUNT] = {4, 5, 6, 7, 3, 2, 1, 0};
	int i, changed = 0;
	uint8_t active;

	if (frame->id == CAN_ID_PDC_VALUE) {
		if (frame->dlc != 8) return 0;
		for (i = 0; i < PDC_SENSOR_COUNT; ++i) {
			if (state->pdc_cm[i] != frame->data[source[i]]) changed = 1;
			state->pdc_cm[i] = frame->data[source[i]];
		}
	} else if (frame->id == CAN_ID_PDC_STATE) {
		if (frame->dlc < 1) return 0;
		if (frame->data[0] != 0x05 && frame->data[0] != 0x06) return 0;
		active = frame->data[0] == 0x05;
		if (state->back_active != active) {
			state->back_active = active;
			changed = 1;
		}
	} else {
		return 0;
	}
	if (changed) state->generation++;
	return changed;
}
