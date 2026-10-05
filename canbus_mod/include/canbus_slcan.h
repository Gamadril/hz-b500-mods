#ifndef CANBUS_SLCAN_H
#define CANBUS_SLCAN_H

#include <stdint.h>

struct canbus_frame {
	uint32_t id;
	uint8_t dlc;
	uint8_t data[8];
};

struct canbus_slcan_parser {
	char line[40];
	uint8_t length;
	uint8_t overflow;
};

/* Returns 1 only after a complete, valid standard data frame. */
int canbus_slcan_feed(struct canbus_slcan_parser *parser, uint8_t byte,
		      struct canbus_frame *frame);

#endif
