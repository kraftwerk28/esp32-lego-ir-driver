#include "esp_attr.h"
#include "esp_check.h"

#include "lego_encoder.h"

#include <string.h>

static const char *TAG = "lego encoder";

static rmt_symbol_word_t start_bit = {
	.level0 = 1,
	.duration0 = 158,
	.level1 = 0,
	.duration1 = 950,
};

static rmt_symbol_word_t end_bit = {
	.level0 = 1,
	.duration0 = 158,
	.level1 = 0,
	.duration1 = 30000,
};

static rmt_symbol_word_t bit_0 = {
	.level0 = 1,
	.duration0 = 158,
	.level1 = 0,
	.duration1 = 263,
};

static rmt_symbol_word_t bit_1 = {
	.level0 = 1,
	.duration0 = 158,
	.level1 = 0,
	.duration1 = 553,
};

static size_t lego_encoder_encode(
	rmt_encoder_t *encoder, rmt_channel_handle_t tx_channel, const void *primary_data,
	size_t data_size, rmt_encode_state_t *ret_state) {
	size_t ret = 0;
	lego_encoder_t *enc = (lego_encoder_t *)encoder;
	lego_packet_t *packets = (lego_packet_t *)primary_data;
	rmt_encode_state_t state = RMT_ENCODING_COMPLETE;
	size_t packet_count = data_size / sizeof(lego_packet_t);

	for (;;) {
		switch (enc->state) {
		case LEGO_START_BIT: {
			ret += enc->copy_encoder->encode(
				enc->copy_encoder, tx_channel, &start_bit, sizeof(start_bit), &state);
			if (state & RMT_ENCODING_MEM_FULL) {
				*ret_state |= RMT_ENCODING_MEM_FULL;
				return ret;
			}
			enc->state = LEGO_WORD;
			break;
		}
		case LEGO_WORD: {
			lego_packet_t p = packets[enc->packet_index];
			enc->packet_index++;

			p.reserved_1 = 0x1;
			switch (p.key) {
			case LEGO_LF:
			case LEGO_LB:
			case LEGO_RF:
			case LEGO_RB:
				p.single_key = true;
				break;
			default:
				p.single_key = false;
				break;
			}
			p.checksum = get_packet_checksum(&p);
			enc->last_packet = p;

			// Big-Endian Byte Order
			// MSB First
			const uint16_t pkt_raw = *(uint16_t *)&p;
			const uint16_t pkt_reversed = (pkt_raw >> 8) | (pkt_raw << 8);

			ret += enc->bytes_encoder->encode(
				enc->bytes_encoder, tx_channel, &pkt_reversed, sizeof(lego_packet_t), &state);
			if (state & RMT_ENCODING_MEM_FULL) {
				*ret_state |= RMT_ENCODING_MEM_FULL;
				return ret;
			}
			enc->state = LEGO_END_BIT;
			break;
		}
		case LEGO_END_BIT: {
			ret += enc->copy_encoder->encode(
				enc->copy_encoder, tx_channel, &end_bit, sizeof(end_bit), &state);
			if (state & RMT_ENCODING_MEM_FULL) {
				*ret_state |= RMT_ENCODING_MEM_FULL;
				return ret;
			}
			enc->state = LEGO_START_BIT;
			if (enc->packet_index == packet_count) {
				enc->packet_index = 0;
				enc->done_packets += enc->packet_index;
				*ret_state = RMT_ENCODING_COMPLETE;
				return ret;
			}
			break;
		}
		}
	}
}

static esp_err_t lego_encoder_del(rmt_encoder_t *encoder) {
	lego_encoder_t *enc = (lego_encoder_t *)encoder;
	ESP_RETURN_ON_ERROR(rmt_del_encoder(enc->bytes_encoder), TAG, "Failed to delete bytes encoder");
	ESP_RETURN_ON_ERROR(rmt_del_encoder(enc->copy_encoder), TAG, "Failed to delete copy encoder");
	return ESP_OK;
}

static esp_err_t lego_encoder_reset(rmt_encoder_t *encoder) {
	lego_encoder_t *enc = (lego_encoder_t *)encoder;
	ESP_RETURN_ON_ERROR(
		rmt_encoder_reset(enc->bytes_encoder), TAG, "Failed to reset bytes encoder");
	ESP_RETURN_ON_ERROR(rmt_encoder_reset(enc->copy_encoder), TAG, "Failed to reset copy encoder");
	enc->state = LEGO_START_BIT;
	enc->done_packets = 0;
	enc->packet_index = 0;
	return ESP_OK;
}

esp_err_t lego_encoder_new(lego_encoder_t *encoder) {
	encoder->base.encode = lego_encoder_encode;
	encoder->base.del = lego_encoder_del;
	encoder->base.reset = lego_encoder_reset;
	encoder->state = LEGO_START_BIT;

	rmt_bytes_encoder_config_t bytes_encoder_cfg = {
		.bit0 = bit_0,
		.bit1 = bit_1,
		.flags.msb_first = true,
	};
	ESP_RETURN_ON_ERROR(
		rmt_new_bytes_encoder(&bytes_encoder_cfg, &encoder->bytes_encoder), TAG,
		"Failed to allocate bytes encoder");

	rmt_copy_encoder_config_t copy_encoder_cfg = {};
	ESP_RETURN_ON_ERROR(
		rmt_new_copy_encoder(&copy_encoder_cfg, &encoder->copy_encoder), TAG,
		"Failed to allocate bytes encoder");

	return ESP_OK;
}
