#ifndef LEGO_ENCODER_INCLUDED
#define LEGO_ENCODER_INCLUDED

#include "driver/rmt_encoder.h"
#include "esp_check.h"

// Packet pseudocode:
//		command = 0;
//		if (two_buttons || stop_command) {
//			command |= 1 << 15;
//		}
//		command |= channel << 12; // channel=0..3
//		command |= 1 << 8;
//		if (left_backward) command |= 1 << 4;
//		if (left_forward) command |= 2 << 4;
//		if (right_forward) command |= 4 << 4;
//		if (right_backward) command |= 8 << 4;
//		command |= 0xf ^ ((command >> 4) & 0xf) ^ ((command >> 8) & 0xf)
//^ (command >> 12); // checksum
//

enum lego_encoder_state {
	LEGO_START_BIT,
	LEGO_WORD,
	LEGO_END_BIT,
};

enum lego_key {
	LEGO_LB = 0x1,
	LEGO_LF = 0x2,
	LEGO_RF = 0x4,
	LEGO_RB = 0x8,
};

typedef struct __attribute__((packed)) {
	uint8_t checksum : 4;
	enum lego_key key : 4;
	uint8_t reserved_1 : 4;
	uint8_t channel : 3;
	bool single_key : 1;
} lego_packet_t;

typedef struct {
	rmt_encoder_t base;
	rmt_encoder_t *copy_encoder;
	rmt_encoder_t *bytes_encoder;
	enum lego_encoder_state state;
	uint32_t packet_index;
	uint32_t done_packets;
	lego_packet_t last_packet;
} lego_encoder_t;

esp_err_t lego_encoder_new(lego_encoder_t *encoder);

static inline uint8_t get_packet_checksum(lego_packet_t *pkt) {
	uint16_t praw = *(uint16_t *)pkt;
	uint8_t ret = 0xf;
	ret ^= (praw >> 12) & 0xf;
	ret ^= (praw >> 8) & 0xf;
	ret ^= (praw >> 4) & 0xf;
	return ret;
}

#define LEGO_STOP_PACKET(ch)                                                                       \
	(lego_packet_t) { .single_key = false, .channel = ch, .key = 0 }

#endif
