#ifndef IR_H_INCLUDED
#define IR_H_INCLUDED

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "defs.h"
#include "lego_encoder.h"
#include "networking.h"

static rmt_channel_handle_t rx_chan = NULL;
static rmt_channel_handle_t tx_chan = NULL;
static rmt_symbol_word_t rx_data[1024] = {0};
static uint32_t rx_data_len = 0;
static lego_encoder_t lego_encoder = {0};

static bool rmt_rx_done_callback(
	rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx) {
	BaseType_t woken = false;
	rx_data_len = edata->num_symbols;
	xEventGroupSetBitsFromISR(egroup, RX_DONE_BIT, &woken);
	return woken == pdTRUE;
}

static void configure_ir_tx(void) {
	const rmt_tx_channel_config_t tx_chan_cfg = {
		.clk_src = RMT_CLK_SRC_DEFAULT,
		.resolution_hz = 1e6,
		.mem_block_symbols = 512,
		.trans_queue_depth = 4,
		.gpio_num = IR_TRX_LED_GPIO,
		.flags.with_dma = false,
		.flags.invert_out = false,
	};
	ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_cfg, &tx_chan));
	assert(tx_chan != NULL);

	const rmt_carrier_config_t tx_carrier_cfg = {
		.duty_cycle = 0.33,
		.frequency_hz = 38000,
		.flags.always_on = false,
		.flags.polarity_active_low = false,
	};
	ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &tx_carrier_cfg));

	ESP_ERROR_CHECK(lego_encoder_new(&lego_encoder));
}

static void ir_tx_task_fn(void *arg) {
	bool is_pressed = false;
	bool end_sent = true;
	BaseType_t bits = 0;
	rmt_transmit_config_t tx_config = {.loop_count = 0};
	// uint8_t nes_buttons = 0;
	uint32_t npackets = 0;
	uint8_t lego_chan = 1;
	uint32_t distance_mm_copy = 0;

	for (;;) {
		if (!xSemaphoreTake(hc_sr04_sem, pdMS_TO_TICKS(1000))) {
			// Shouldn't happen
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}
		distance_mm_copy = distance_mm;
		xSemaphoreGive(hc_sr04_sem);

		if (distance_mm_copy > 400) {
			end_sent = false;
			packets[0] = (lego_packet_t){
				.key = LEGO_LF | LEGO_RF,
				.channel = lego_chan,
			};
			npackets = 1;
		} else if (distance_mm_copy < 200) {
			end_sent = false;
			packets[0] = (lego_packet_t){
				.key = LEGO_LB | LEGO_RB,
				.channel = lego_chan,
			};
			npackets = 1;
		} else if (!end_sent) {
			packets[0] = LEGO_STOP_PACKET(lego_chan);
			packets[1] = LEGO_STOP_PACKET(lego_chan);
			npackets = 2;
			end_sent = true;
		} else {
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		// if (!xQueueReceive(nes_button_queue, &nes_buttons, 1000 /
		// portTICK_PERIOD_MS)) { 	continue;
		// }

		// BaseType_t new_bits = xEventGroupWaitBits(egroup, GPIO_BTN_DOWN_BIT |
		// GPIO_BTN_UP_BIT, true, false, 0); if (new_bits != 0) { 	bits =
		// new_bits;
		// }

		// // Controlling with NES controller
		// if (nes_buttons == 0x00 && !end_sent) {
		// 	packets[0] = LEGO_STOP_PACKET(lego_chan);
		// 	packets[1] = LEGO_STOP_PACKET(lego_chan);
		// 	npackets = 2;
		// 	end_sent = true;
		// } else if (nes_buttons != 0) {
		// 	end_sent = false;
		// 	lego_packet_t move_pkt = {.key = 0, .channel = lego_chan};
		// 	if (nes_buttons & 0x10) {
		// 		move_pkt.key = LEGO_LF | LEGO_RF;
		// 	} else if (nes_buttons & 0x20) {
		// 		move_pkt.key = LEGO_LB | LEGO_RB;
		// 	} else if (nes_buttons & 0x40) {
		// 		move_pkt.key = LEGO_RF;
		// 	} else if (nes_buttons & 0x80) {
		// 		move_pkt.key = LEGO_LF;
		// 	}
		// 	packets[0] = move_pkt;
		// 	npackets = 1;
		// } else {
		// 	continue;
		// }

		// if (bits & GPIO_BTN_DOWN_BIT) {
		// 	// if (!end_sent) {
		// 	// 	// Prevent frequent key packets
		// 	// 	vTaskDelay(100 / portTICK_PERIOD_MS);
		// 	// }
		// 	// ESP_LOGI("lego:tx:flow", "Sending key packet");
		// 	end_sent = false;
		// 	packets[0] = (lego_packet_t){.channel = lego_chan, .key = LEGO_LF |
		// LEGO_RF}; 	npackets = 1; } else if ((bits & GPIO_BTN_UP_BIT) &&
		// !end_sent) {
		// 	// ESP_LOGI("lego:tx:flow", "Sending stop packet");
		// 	end_sent = true;
		// 	packets[0] = LEGO_STOP_PACKET(lego_chan);
		// 	packets[1] = LEGO_STOP_PACKET(lego_chan);
		// 	npackets = 2;
		// } else {
		// 	vTaskDelay(50 / portTICK_PERIOD_MS);
		// 	continue;
		// }

		ESP_ERROR_CHECK(rmt_transmit(
			tx_chan, &lego_encoder.base, packets, sizeof(lego_packet_t) * npackets, &tx_config));
		ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx_chan, 2000 / portTICK_PERIOD_MS));
		// ESP_LOGI("lego:tx", "Sent lego packets. Total sent packets: %lu",
		// lego_encoder.done_packets);
		for (uint32_t i = 0; i < npackets; i++) {
			LEGO_PACKET_DUMP("lego:tx", packets[i]);
		}
		LEGO_PACKET_DUMP("lego:tx:last_pkt", lego_encoder.last_packet);
	}
}

static void lego_controller_task_fn(void *arg) {
	const rmt_transmit_config_t tx_config = {
		.loop_count = 0,
	};
	for (;;) {
		if (lego_state.pressed_button != 0) {
			lego_state.pressed_button_end_sent = false;
			lego_packet_t pkt = {
				.key = lego_state.pressed_button,
				.channel = lego_state.channel,
			};
			ESP_ERROR_CHECK(
				rmt_transmit(tx_chan, &lego_encoder.base, &pkt, sizeof(lego_packet_t), &tx_config));
			rmt_tx_wait_all_done(tx_chan, 10000);
			ESP_LOGI("lego", "Sent buttons");
			LEGO_PACKET_DUMP("lego", pkt);
			continue;
		} else if (lego_state.pressed_button == 0 && !lego_state.pressed_button_end_sent) {
			lego_state.pressed_button_end_sent = true;
			lego_packet_t pkt[] = {
				LEGO_STOP_PACKET(lego_state.channel), LEGO_STOP_PACKET(lego_state.channel)};
			ESP_ERROR_CHECK(rmt_transmit(
				tx_chan, &lego_encoder.base, &pkt, sizeof(lego_packet_t) * 2, &tx_config));
			rmt_tx_wait_all_done(tx_chan, 10000);
			ESP_LOGI("lego", "Sent release");
			LEGO_PACKET_DUMP("lego", pkt[0]);
			continue;
		}

		const EventBits_t bits = xEventGroupWaitBits(
			egroup, LEGO_PKT_FLUSH_BIT | LEGO_PKT_CONT_BIT, true, false, portMAX_DELAY);
		if (bits == 0 || (bits & LEGO_PKT_CONT_BIT))
			continue;
		if (bits & LEGO_PKT_FLUSH_BIT) {
			const rmt_transmit_config_t tx_config = {
				.loop_count = 0,
			};
			for (uint32_t i = 0; i < lego_state.npackets; i++) {
				lego_state.packets[i].channel = lego_state.channel;
			}
			ESP_ERROR_CHECK(rmt_transmit(
				tx_chan, &lego_encoder.base, lego_state.packets,
				sizeof(lego_packet_t) * lego_state.npackets, &tx_config));
			const esp_err_t tx_result = rmt_tx_wait_all_done(tx_chan, 10000);
			mqtt_publish_result(tx_result);
			if (tx_result == ESP_OK)
				ESP_LOGI("lego", "Sent %lu packets", lego_state.npackets);
			lego_state.npackets = 0;
		}
	}
}

static void configure_ir_rx(void) {
	const rmt_rx_channel_config_t rx_chan_config = {
		.clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
		.resolution_hz = 1e6,			// 1MHz tick resolution, i.e. 1 tick = 1us
		.mem_block_symbols = 64,		// memory block size, 64 * 4 = 256Bytes
		.gpio_num = GPIO_NUM_14,		// GPIO number
		.flags.invert_in = false,		// don't invert input signal
		.flags.with_dma = false,		// don't need DMA backend
	};
	ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));
	assert(rx_chan != NULL);

	rmt_rx_event_callbacks_t callbacks = {
		.on_recv_done = rmt_rx_done_callback,
	};
	ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &callbacks, NULL));
}

static void ir_rx_task_fn(void *data) {
	static rmt_receive_config_t rx_config = {
		.signal_range_min_ns = /* 1250 */ 100 * 1e3,
		.signal_range_max_ns = /* 12000 * 1e3 */ 30000 * 1e3,
	};

	for (;;) {
		ESP_ERROR_CHECK(
			rmt_receive(rx_chan, rx_data, sizeof(rx_data) / sizeof(rx_data[0]), &rx_config));

		if (xEventGroupWaitBits(egroup, RX_DONE_BIT, true, true, portMAX_DELAY) != RX_DONE_BIT) {
			// ESP_LOGI(TAG, "Timed out while receiving IR data");
			continue;
		}

		// Channel 1, snapshot 1:
		// LF:		0x8124
		// LB:		0x8117
		// RF:		0x8142
		// RB:		0x818e
		// FF:		0x0168
		// BB:		0x0197
		// LFRB:	0x01a4
		// LBRF:	0x015b
		// STOP:	0x010e
		//
		// Channel 2, snapshot 1:
		// LF:		0x9125
		// LB:		0x9116
		// RF:		0x9143
		// RB:		0x918f
		// FF:		0x1169
		// BB:		0x1196
		// LFRB:	0x11a5
		// LBRF:	0x115a
		// STOP:	0x110f
		//
		// Channel 3, snapshot 1
		// LF:		0xa126
		// LB:		0xa115
		// RF:		0xa140
		// RB:		0xa18c
		// FF:		0x216a
		// BB:		0x2195
		// LFRB:	0x21a6
		// LBRF:	0x2159
		// STOP:	0x210c
		//
		// Channel 4, snapshot 1
		// LF:		0xb127
		// LB:		0xb114
		// RF:		0xb141
		// RB:		0xb18d
		// FF:		0x316b
		// BB:		0x3194
		// LFRB:	0x31a7
		// LBRF:	0x3158
		// STOP:	0x310d
		//
		//
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
		//		uint8_t checksum = 0xf;
		//		checksum ^= (command >> 12) & 0xf;
		//		checksum ^= (command >> 8) & 0xf;
		//		checksum ^= (command >> 4) & 0xf;
		//		command |= checksum;
		//

		// ESP_LOGI("lego:rx:tim", "Raw timings:");
		// for (uint32_t i = 0; i < rx_data_len; i++) {
		// 	rmt_symbol_word_t w = rx_data[i];
		// 	ESP_LOGI(
		// 		"lego:rx:tim", "%3lu. duration0=%u level0=%u duration1=%u level1=%u
		// val=%u", i + 1, w.duration0, 		w.level0, w.duration1, w.level1, w.val);
		// }

		uint32_t i = 0;
		for (; i < rx_data_len;) {
			if (rx_data[i].duration1 >= 800 && rx_data[i].duration1 < 1100) {
				i++;
				uint16_t value = 0;
				for (uint8_t j = 0; j < 16; j++) {
					if (rx_data[i + j].duration1 >= 320) {
						value |= 1 << (15 - j);
					}
					// const uint8_t bit = rx_data[i + j].duration1 < 320 ? 0 :
					// 1; value |= bit << (15 - j);
				}
				lego_packet_t pkt = *(lego_packet_t *)&value;
				LEGO_PACKET_DUMP("lego:rx", pkt);
				if (get_packet_checksum(&pkt) != pkt.checksum) {
					ESP_LOGE(
						"lego:rx", "Checksum: INVALID (exp=0x%x real=0x%x)",
						get_packet_checksum(&pkt), pkt.checksum);
				} else {
					ESP_LOGI("lego:rx", "Checksum: OK (0x%x)", pkt.checksum);
				}
			} else {
				i++;
			}
		}
		ESP_LOGI("lego:rx", "");
		ESP_LOGI("lego:rx", "");
	}
}

#endif
