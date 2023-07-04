#include <string.h>

#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_private/esp_clk.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "defs.h"
#include "lego_encoder.h"

#include "ir.h"
#include "networking.h"

static esp_err_t publish_led_state(void) {
	esp_err_t err;
	// uint32_t response_payload_len = sprintf(NULL, LED_STATE_FMT);
	char *response_payload = malloc(128);
	uint32_t response_payload_len = snprintf(response_payload, 128, LED_STATE_FMT);
	err = esp_mqtt_client_publish(
		mqtt_handle, "esp/status", response_payload, response_payload_len, 0, 1);
	free(response_payload);
	return err;
}

// Basic anti-glitch GPIO filter using timer
static void gpio_isr_handler(void *data) {
	uint32_t period = 100 * 1e3;
	if (esp_timer_is_active(gpio_glitch_timer_handle)) {
		ESP_ERROR_CHECK(esp_timer_restart(gpio_glitch_timer_handle, period));
	} else {
		ESP_ERROR_CHECK(esp_timer_start_once(gpio_glitch_timer_handle, period));
	}
}

static void gpio_glitch_timer_callback(void *arg) {
	gpio_num_t gpio = (gpio_num_t)arg;
	if (!gpio_get_level(gpio)) {
		xEventGroupSetBitsFromISR(egroup, GPIO_BTN_DOWN_BIT, NULL);
	} else {
		xEventGroupSetBitsFromISR(egroup, GPIO_BTN_UP_BIT, NULL);
	}
}

static void button_task_fn(void *arg) {
	for (;;) {
		if (xEventGroupWaitBits(egroup, GPIO_BTN_DOWN_BIT | GPIO_BTN_UP_BIT, true, false, 1000) ==
			0)
			continue;
		ESP_LOGI("lego:button", "GPIO0=%u", gpio_get_level(GPIO_NUM_0));
	}
}

static void configure_button(void) {
	gpio_num_t btn = GPIO_NUM_0;
	ESP_ERROR_CHECK(gpio_reset_pin(btn));
	ESP_ERROR_CHECK(gpio_set_direction(btn, GPIO_MODE_INPUT));
	ESP_ERROR_CHECK(gpio_set_pull_mode(btn, GPIO_PULLUP_ONLY));
	ESP_ERROR_CHECK(gpio_intr_enable(btn));
	ESP_ERROR_CHECK(gpio_set_intr_type(btn, GPIO_INTR_ANYEDGE));
	ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));
	ESP_ERROR_CHECK(gpio_isr_handler_add(btn, gpio_isr_handler, NULL));

	esp_timer_create_args_t timer_cfg = {
		.callback = gpio_glitch_timer_callback,
		.arg = (void *)GPIO_NUM_0,
	};
	ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &gpio_glitch_timer_handle));
}

static void nes_timer_master_callback(void *arg) {
	ESP_ERROR_CHECK(esp_timer_start_periodic(nes_timer_handle[1], 6));
	return;
}

static void nes_timer_slave_callback(void *arg) {
	if (nes_state == 0) {
		nes_buttons = 0;
	}
	if (nes_state % 2 == 0) {
		gpio_set_level(nes_state == 0 ? nes_latch : nes_clk, 1);
	} else {
		gpio_set_level(nes_state == 1 ? nes_latch : nes_clk, 0);
		nes_buttons |= gpio_get_level(nes_miso) << (nes_state / 2);
	}
	nes_state++;
	if (nes_state == 16) {
		const uint8_t buttons_inv = ~nes_buttons;
		xQueueSendFromISR(nes_button_queue, &buttons_inv, NULL);
		nes_state = 0;
		ESP_ERROR_CHECK(esp_timer_stop(nes_timer_handle[1]));
	}

	// if (nes_state == NES_STATE_RESET) {
	// 	ESP_ERROR_CHECK(gpio_set_level(nes_latch, 1));
	// 	nes_buttons = 0;
	// } else if (nes_state == 1) {
	// 	ESP_ERROR_CHECK(gpio_set_level(nes_latch, 0));
	// 	nes_buttons |= gpio_get_level(nes_miso);
	// } else if (nes_state > 1 && nes_state % 2 == 0) {
	// 	ESP_ERROR_CHECK(gpio_set_level(nes_clk, 1));
	// } else if (nes_state > 1 && nes_state % 2 == 1) {
	// 	ESP_ERROR_CHECK(gpio_set_level(nes_clk, 0));
	// 	nes_buttons |= gpio_get_level(nes_miso) << ((nes_state - 1) / 2);
	// }
	// nes_state++;
	// if (nes_state == 16) {
	// 	const uint8_t buttons_inv = ~nes_buttons;
	// 	xQueueSendFromISR(nes_button_queue, &buttons_inv, NULL);
	// 	nes_state = NES_STATE_RESET;
	// 	ESP_ERROR_CHECK(esp_timer_stop(nes_timer_handle[1]));
	// }
}

static void configure_nes(void) {
	gpio_reset_pin(nes_latch);
	gpio_reset_pin(nes_clk);
	gpio_reset_pin(nes_miso);
	gpio_set_direction(nes_latch, GPIO_MODE_OUTPUT);
	gpio_set_pull_mode(nes_latch, GPIO_PULLDOWN_ONLY);
	gpio_set_direction(nes_clk, GPIO_MODE_OUTPUT);
	gpio_set_pull_mode(nes_clk, GPIO_PULLDOWN_ONLY);
	gpio_set_direction(nes_miso, GPIO_MODE_INPUT);
	gpio_set_pull_mode(nes_miso, GPIO_FLOATING);

	nes_button_queue = xQueueCreate(10, sizeof(nes_buttons));
	assert(nes_button_queue != NULL);
	ESP_ERROR_CHECK(esp_timer_create(
		&(esp_timer_create_args_t){
			.callback = nes_timer_master_callback,
		},
		&nes_timer_handle[0]));
	ESP_ERROR_CHECK(esp_timer_start_periodic(nes_timer_handle[0], 1e6 / 50));
	ESP_ERROR_CHECK(esp_timer_create(
		&(esp_timer_create_args_t){
			.callback = nes_timer_slave_callback,
		},
		&nes_timer_handle[1]));
};

static void nes_task_fn(void *arg) {
	uint8_t old_buttons = 0, buttons = 0;
	for (;;) {
		if (!xQueueReceive(nes_button_queue, &buttons, 1000 / portTICK_PERIOD_MS))
			continue;

		if (buttons == old_buttons)
			continue;

		old_buttons = buttons;
		char buttons_str[9] = {'A', 'B', 'S', 'S', 'U', 'D', 'L', 'R', 0};
		for (uint8_t i = 0; i < 8; i++) {
			if (((buttons >> i) & 1) == 0) {
				buttons_str[i] = ' ';
			}
		}
		ESP_LOGI("lego:nes", "raw=%02x buttons=%s", buttons, buttons_str);
	}
}

static bool pwm_capture_callback(
	mcpwm_cap_channel_handle_t cap_channel, const mcpwm_capture_event_data_t *edata,
	void *user_ctx) {
	BaseType_t woken = pdFALSE;
	switch (edata->cap_edge) {
	case MCPWM_CAP_EDGE_POS:
		capture_positive = edata->cap_value;
		break;
	case MCPWM_CAP_EDGE_NEG: {
		if (xSemaphoreTakeFromISR(hc_sr04_sem, &woken)) {
			const uint32_t delta = edata->cap_value - capture_positive;
			distance_mm = (uint32_t)((float)(delta / (esp_clk_apb_freq() / 1e6)) / 5.8);
			capture_positive = 0;
			xSemaphoreGiveFromISR(hc_sr04_sem, &woken);
		}
		break;
	}
	}
	return woken == pdTRUE;
}

static void pwm_trig_timer_callback(void *arg) {
	ESP_ERROR_CHECK(gpio_set_level(HC_SR04_TRIG_GPIO, 0));
	return;
}

static void configure_hc_sr04(void) {
	const mcpwm_capture_timer_config_t timer_cfg = {
		.group_id = 0,
		.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
	};
	ESP_ERROR_CHECK(mcpwm_new_capture_timer(&timer_cfg, &hc_sr04_mcpwm_capture_timer_handle));

	const mcpwm_capture_channel_config_t chan_cfg = {
		.prescale = 1,
		// .prescale = esp_clk_apb_freq() / 1e6,
		.gpio_num = HC_SR04_ECHO_GPIO,
		.flags.pos_edge = true,
		.flags.neg_edge = true,
	};
	ESP_ERROR_CHECK(mcpwm_new_capture_channel(
		hc_sr04_mcpwm_capture_timer_handle, &chan_cfg, &hc_sr04_mcpwm_capture_channel_handle));

	const mcpwm_capture_event_callbacks_t cbs = {
		.on_cap = pwm_capture_callback,
	};
	ESP_ERROR_CHECK(mcpwm_capture_channel_register_event_callbacks(
		hc_sr04_mcpwm_capture_channel_handle, &cbs, NULL));

	const esp_timer_create_args_t trig_timer_cfg = {
		.callback = pwm_trig_timer_callback,
	};
	ESP_ERROR_CHECK(esp_timer_create(&trig_timer_cfg, &hc_sr04_trig_timer_handle));
}

static void hs_sr04_task_fn(void *arg) {
	uint32_t value = 0;
	ESP_ERROR_CHECK(gpio_reset_pin(HC_SR04_TRIG_GPIO));
	ESP_ERROR_CHECK(gpio_set_direction(HC_SR04_TRIG_GPIO, GPIO_MODE_OUTPUT));
	ESP_ERROR_CHECK(gpio_set_level(HC_SR04_TRIG_GPIO, 0));
	for (;;) {
		ESP_ERROR_CHECK(gpio_set_level(HC_SR04_TRIG_GPIO, 1));
		ESP_ERROR_CHECK(esp_timer_start_once(hc_sr04_trig_timer_handle, 10));
		// if (xTaskNotifyWait(0, ~0, &value, pdMS_TO_TICKS(10000))) {
		// 	float distance_mm = (float)(value / (esp_clk_apb_freq() / 1e6))
		// / 5.8; 	ESP_LOGI("hs_sr04", "value=%lu distance=%f", value,
		// distance_mm); } else { 	ESP_LOGE("hs_sr04", "Timed out while waiting
		// for echo");
		// }
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

void app_main(void) {
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	lego_state.npackets = 0;
	lego_state.channel = 1;

	egroup = xEventGroupCreate();
	assert(egroup != NULL);

	hc_sr04_sem = xSemaphoreCreateMutex();
	assert(hc_sr04_sem != NULL);

	const gpio_config_t gpio_cfg = {
		.pin_bit_mask = (uint64_t)1 << GPIO_NUM_4 | (uint64_t)1 << GPIO_NUM_33,
		.mode = GPIO_MODE_OUTPUT,
		.intr_type = GPIO_INTR_DISABLE,
	};
	ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
	// GPIO33 is pulled up
	gpio_set_level(GPIO_NUM_33, 1);

	configure_ir_tx();
	// configure_ir_rx();
	// configure_button();
	// configure_uart();
	// configure_nes();
	// configure_hc_sr04();
	configure_wifi();
	configure_mqtt();

	// NOTE: Lego IR Transceiver peripherals
	ESP_ERROR_CHECK(rmt_enable(tx_chan));
	// ESP_ERROR_CHECK(rmt_enable(rx_chan));

	// NOTE: HS-SR04 peripherals
	// ESP_ERROR_CHECK(mcpwm_capture_timer_enable(hc_sr04_mcpwm_capture_timer_handle));
	// ESP_ERROR_CHECK(mcpwm_capture_channel_enable(hc_sr04_mcpwm_capture_channel_handle));
	// ESP_ERROR_CHECK(mcpwm_capture_timer_start(hc_sr04_mcpwm_capture_timer_handle));

	// assert(xTaskCreate(ir_tx_task_fn, "ir_tx", 2048, NULL, 10, NULL) == pdPASS);
	assert(xTaskCreate(lego_controller_task_fn, "lego_controller", 2048, NULL, 10, NULL) == pdPASS);
	// assert(xTaskCreate(ir_rx_task_fn, "ir_rx", 2048, NULL, 10, NULL) == pdPASS);
	// assert(xTaskCreate(button_task_fn, "button", 2048, NULL, 10, NULL) == pdPASS);
	// assert(xTaskCreate(nes_task_fn, "nes", 2048, NULL, 10, NULL) == pdPASS);
	// assert(xTaskCreate(hs_sr04_task_fn, "hs_sr04", 2048, NULL, 10, NULL) == pdPASS);
	assert(xTaskCreate(wifi_task_fn, "wifi", 1024, NULL, 10, NULL) == pdPASS);
}
