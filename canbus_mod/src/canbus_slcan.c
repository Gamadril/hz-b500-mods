#include "canbus_slcan.h"

static int hex_digit(char ch)
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	return -1;
}

static int decode_line(const char *line, unsigned length,
		       struct canbus_frame *frame)
{
	unsigned i, id = 0, dlc, payload_end;
	int digit;

	if (length < 5 || line[0] != 't') return 0;
	for (i = 1; i < 4; ++i) {
		digit = hex_digit(line[i]);
		if (digit < 0) return 0;
		id = (id << 4) | (unsigned)digit;
	}
	if (id > 0x7ff || line[4] < '0' || line[4] > '8') return 0;
	dlc = (unsigned)(line[4] - '0');
	payload_end = 5 + 2 * dlc;
	if (length != payload_end && length != payload_end + 4) return 0;
	for (i = 5; i < length; ++i)
		if (hex_digit(line[i]) < 0) return 0;
	frame->id = id;
	frame->dlc = (uint8_t)dlc;
	for (i = 0; i < dlc; ++i)
		frame->data[i] = (uint8_t)((hex_digit(line[5 + 2 * i]) << 4) |
					    hex_digit(line[6 + 2 * i]));
	return 1;
}

int canbus_slcan_feed(struct canbus_slcan_parser *parser, uint8_t byte,
		      struct canbus_frame *frame)
{
	int valid;

	if (byte == '\n') {
		if (parser->length) parser->overflow = 1;
		return 0;
	}
	if (byte == '\a') {
		parser->length = 0;
		parser->overflow = 0;
		return 0;
	}
	if (byte == '\r') {
		valid = !parser->overflow &&
			decode_line(parser->line, parser->length, frame);
		parser->length = 0;
		parser->overflow = 0;
		return valid;
	}
	if (parser->overflow) return 0;
	if (parser->length == sizeof(parser->line)) {
		parser->overflow = 1;
		return 0;
	}
	parser->line[parser->length++] = (char)byte;
	return 0;
}
