#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "canbus_slcan.h"
#include "canbus_pdc.h"

static int feed(struct canbus_slcan_parser *parser, const char *text,
		struct canbus_frame *frame)
{
	int frames = 0;
	while (*text)
		frames += canbus_slcan_feed(parser, (uint8_t)*text++, frame);
	return frames;
}

int main(void)
{
	struct canbus_slcan_parser parser = {{0}, 0, 0};
	struct canbus_frame frame;
	struct canbus_ui_state state = {{255, 255, 255, 255,
					 255, 255, 255, 255}, 0, 0};
	char too_long[70];

	assert(feed(&parser, "t1C28aabb", &frame) == 0);
	assert(feed(&parser, "ccddeeff0011\r", &frame) == 1);
	assert(frame.id == 0x1c2 && frame.dlc == 8);
	assert(frame.data[0] == 0xaa && frame.data[7] == 0x11);
	assert(canbus_pdc_apply(&state, &frame) == 1);
	assert(state.pdc_cm[PDC_FL] == 0xee);
	assert(state.pdc_cm[PDC_FLM] == 0xff);
	assert(state.pdc_cm[PDC_FRM] == 0x00);
	assert(state.pdc_cm[PDC_FR] == 0x11);
	assert(state.pdc_cm[PDC_RR] == 0xdd);
	assert(state.pdc_cm[PDC_RRM] == 0xcc);
	assert(state.pdc_cm[PDC_RLM] == 0xbb);
	assert(state.pdc_cm[PDC_RL] == 0xaa);
	assert(canbus_pdc_apply(&state, &frame) == 0);

	assert(feed(&parser, "\nt24A1051234\rt24A106\r", &frame) == 2);
	assert(frame.id == 0x24a && frame.dlc == 1 && frame.data[0] == 6);
	assert(feed(&parser, "t24a105\r", &frame) == 1);
	assert(frame.id == 0x24a && frame.data[0] == 5);
	assert(canbus_pdc_apply(&state, &frame) == 1);
	assert(state.back_active == 1);
	assert(canbus_pdc_apply(&state, &frame) == 0);
	assert(feed(&parser, "t24A107\r", &frame) == 1);
	assert(canbus_pdc_apply(&state, &frame) == 0 && state.back_active == 1);
	assert(feed(&parser, "t24A106\r", &frame) == 1);
	assert(canbus_pdc_apply(&state, &frame) == 1);
	assert(state.back_active == 0 && state.generation == 3);

	assert(feed(&parser, "t24A1GG\r", &frame) == 0);
	assert(feed(&parser, "t24A205\r", &frame) == 0);
	assert(feed(&parser, "t800105\r", &frame) == 0);
	assert(feed(&parser, "T0000024A105\r", &frame) == 0);
	assert(feed(&parser, "r24A1\r", &frame) == 0);
	assert(feed(&parser, "t24\nA105\r", &frame) == 0);
	assert(feed(&parser, "\r\a", &frame) == 0);

	memset(too_long, 'A', sizeof(too_long));
	too_long[sizeof(too_long) - 2] = '\r';
	too_long[sizeof(too_long) - 1] = '\0';
	assert(feed(&parser, too_long, &frame) == 0);
	assert(feed(&parser, "t24A105\r", &frame) == 1);
	return 0;
}
