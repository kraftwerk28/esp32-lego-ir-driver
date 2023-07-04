#ifndef DEFS_H_INCLUDED
#define DEFS_H_INCLUDED

#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "esp_timer.h"

#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "lego_encoder.h"

//
// Defines
//
#define RX_DONE_BIT 1 << 0
#define GPIO_BTN_DOWN_BIT 1 << 1
#define GPIO_BTN_UP_BIT 1 << 2
#define WIFI_STARTED_BIT 1 << 3
#define WIFI_CONNECTED_BIT 1 << 4
#define WIFI_DISCONNECTED_BIT 1 << 5
#define IP_GOT_IP_BIT 1 << 6
#define MQTT_CONNECTED_BIT 1 << 7
#define LEGO_PKT_FLUSH_BIT 1 << 8
#define LEGO_PKT_CONT_BIT 1 << 9

#define HC_SR04_TRIG_GPIO GPIO_NUM_2
#define HC_SR04_ECHO_GPIO GPIO_NUM_14
#define IR_TRX_LED_GPIO GPIO_NUM_15

#define MQTT_URI "mqtt://192.168.0.110:1883"

#define WIFI_SSID "dude"
#define WIFI_PASSWORD "ark351wsm294w"

//
// Macros
//
#define LED_STATE_FMT                                                                              \
	"{\"led\":%s,\"flash\":%s}", !gpio_get_level(GPIO_NUM_33) ? "true" : "false",                  \
		gpio_get_level(GPIO_NUM_4) ? "true" : "false"

#define LEGO_PACKET_DUMP(tag, pkt)                                                                 \
	do {                                                                                           \
		uint16_t pkt_raw = *(uint16_t *)(&pkt);                                                    \
		char key_str[5] = {0};                                                                     \
		for (int __bit_index = 0; __bit_index < 4; __bit_index++) {                                \
			if ((pkt).key & (1 << (3 - __bit_index))) {                                            \
				key_str[__bit_index] = '1';                                                        \
			} else {                                                                               \
				key_str[__bit_index] = '0';                                                        \
			}                                                                                      \
		}                                                                                          \
		ESP_LOGI(                                                                                  \
			tag ":packet", "Lego Packet 0x%04x: single_key=%d channel=%u keys=%s", pkt_raw,        \
			(pkt).single_key, (pkt).channel + 1, key_str);                                         \
	} while (false);

//
// Globals
//
struct lego_state {
	uint8_t channel;
	lego_packet_t packets[128];
	uint32_t npackets;
	enum lego_key pressed_button;
	bool pressed_button_end_sent;
} lego_state = {0};

static EventGroupHandle_t egroup = NULL;

static esp_timer_handle_t gpio_glitch_timer_handle = NULL;

static esp_timer_handle_t nes_timer_handle[2] = {0};
static int8_t nes_state = 0;
static gpio_num_t nes_clk = GPIO_NUM_13;
static gpio_num_t nes_latch = GPIO_NUM_15;
static gpio_num_t nes_miso = GPIO_NUM_14;
static uint8_t nes_buttons = 0;
static QueueHandle_t nes_button_queue = NULL;

static lego_packet_t packets[8] = {0};
static mcpwm_cap_timer_handle_t hc_sr04_mcpwm_capture_timer_handle = NULL;
static mcpwm_cap_channel_handle_t hc_sr04_mcpwm_capture_channel_handle = NULL;
static esp_timer_handle_t hc_sr04_trig_timer_handle = NULL;
static uint32_t capture_positive = 0;
static uint32_t distance_mm = 0;
static SemaphoreHandle_t hc_sr04_sem = NULL;

#endif
