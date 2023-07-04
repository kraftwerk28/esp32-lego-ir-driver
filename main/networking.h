#ifndef WIFI_H_INCLUDED
#define WIFI_H_INCLUDED

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"

#include "defs.h"
#include "lego_encoder.h"

static esp_netif_t *wifi_netif = NULL;
static esp_mqtt_client_handle_t mqtt_handle = NULL;

#define MKTOPIC(t) ("esp/1/" t)

static void esp_mqtt_event_callback(
	void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	if (event_id == MQTT_EVENT_CONNECTED) {
		// esp_mqtt_client_subscribe(mqtt_handle, "esp/led/+", 0);
		// esp_mqtt_client_subscribe(mqtt_handle, "esp/flash/+", 0);
		esp_mqtt_client_subscribe(mqtt_handle, MKTOPIC("lego/cmd/append"), 0);
		esp_mqtt_client_subscribe(mqtt_handle, MKTOPIC("lego/button"), 0);
		esp_mqtt_client_subscribe(mqtt_handle, MKTOPIC("gpio/+/set/+"), 0);
		esp_mqtt_client_publish(mqtt_handle, MKTOPIC("status"), "alive", 0, 0, true);
		xEventGroupSetBits(egroup, MQTT_CONNECTED_BIT);
	} else if (event_id == MQTT_EVENT_DISCONNECTED) {
		ESP_LOGI("lego:mqtt", "MQTT disconnected");
		vTaskDelay(pdMS_TO_TICKS(1000));
		ESP_ERROR_CHECK(esp_mqtt_client_reconnect(mqtt_handle));
	} else if (event_id == MQTT_EVENT_DATA) {
		esp_mqtt_event_t *e = event_data;
		uint32_t gpio_num = 0, gpio_level = 0;

		if (strncmp(e->topic, MKTOPIC("lego/cmd/append"), e->topic_len) == 0) {
			uint32_t npackets = e->data_len / sizeof(lego_packet_t);
			ESP_LOGI("wifi", "Received %lu Lego packets", npackets);
			// for (uint32_t i = 0; i < pkt_count; i++) {
			// 	lego_packet_t p = ((lego_packet_t *)e->data)[i];
			// 	LEGO_PACKET_DUMP("wifi", p);
			// }
			memcpy(
				&lego_state.packets[lego_state.npackets], e->data,
				npackets * sizeof(lego_packet_t));
			lego_state.npackets += npackets;
			if (lego_state.npackets > 0) {
				xEventGroupSetBits(egroup, LEGO_PKT_FLUSH_BIT);
			}
		} else if (strncmp(e->topic, MKTOPIC("lego/cmd/flush"), e->topic_len) == 0) {
			if (lego_state.npackets == 0) {
				ESP_LOGW("wifi", "Received flush, but the queue is empty");
			} else {
				xEventGroupSetBits(egroup, LEGO_PKT_FLUSH_BIT);
			}
		} else if (strncmp(e->topic, MKTOPIC("lego/button"), e->topic_len) == 0) {
			enum lego_key keys = *e->data;
			lego_state.pressed_button = keys;
			xEventGroupSetBits(egroup, LEGO_PKT_CONT_BIT);
		} else if (sscanf(e->topic, MKTOPIC("gpio/%lu/set/%lu"), &gpio_num, &gpio_level) == 2) {
			ESP_LOGI("mqtt", "Setting GPIO=%lu to level %lu", gpio_num, gpio_level);
			gpio_set_level(gpio_num, gpio_level);
		}
	}
}

static void esp_generic_event_callback(
	void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	if (event_base == WIFI_EVENT) {
		if (event_id == WIFI_EVENT_STA_START) {
			xEventGroupSetBits(egroup, WIFI_STARTED_BIT);
		} else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
			xEventGroupSetBits(egroup, WIFI_DISCONNECTED_BIT);
		} else if (event_id == WIFI_EVENT_STA_CONNECTED) {
			xEventGroupSetBits(egroup, WIFI_CONNECTED_BIT);
		}
	} else if (event_base == IP_EVENT) {
		if (event_id == IP_EVENT_STA_GOT_IP) {
			xEventGroupSetBits(egroup, IP_GOT_IP_BIT);
		}
	}
}

static void configure_wifi(void) {
	wifi_netif = esp_netif_create_default_wifi_sta();
	assert(wifi_netif != NULL);

	wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
	ESP_ERROR_CHECK(
		esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, esp_generic_event_callback, NULL));
	ESP_ERROR_CHECK(
		esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, esp_generic_event_callback, NULL));

	wifi_config_t wifi_cfg = {
		.sta.ssid = WIFI_SSID,
		.sta.password = WIFI_PASSWORD,
		.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

	ESP_ERROR_CHECK(esp_wifi_start());
	assert(
		xEventGroupWaitBits(egroup, WIFI_STARTED_BIT, true, true, pdMS_TO_TICKS(5000)) ==
		WIFI_STARTED_BIT);

	ESP_ERROR_CHECK(esp_wifi_connect());
	assert(
		xEventGroupWaitBits(
			egroup, WIFI_CONNECTED_BIT | IP_GOT_IP_BIT, true, true, pdMS_TO_TICKS(60 * 1e3)) ==
		(WIFI_CONNECTED_BIT | IP_GOT_IP_BIT));
	ESP_LOGI("wifi", "WiFi is ready");
}

static void configure_mqtt(void) {
	const esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = MQTT_URI,
		.session.last_will =
			{
				.topic = MKTOPIC("status"),
				.msg = "dead",
				.msg_len = 0,
				.retain = true,
				.qos = 0,
			},
		.session.disable_clean_session = false,
		.session.keepalive = 5,
		.session.protocol_ver = MQTT_PROTOCOL_V_5,
	};
	mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);
	assert(mqtt_handle != NULL);
	ESP_ERROR_CHECK(esp_mqtt_client_register_event(
		mqtt_handle, ESP_EVENT_ANY_ID, esp_mqtt_event_callback, NULL));
	ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_handle));
	assert(xEventGroupWaitBits(egroup, MQTT_CONNECTED_BIT, true, true, pdMS_TO_TICKS(10000)));
	ESP_LOGI("mqtt", "MQTT is ready");
}

static void wifi_task_fn(void *arg) {
	for (;;) {
		EventBits_t bits = xEventGroupWaitBits(
			egroup, WIFI_DISCONNECTED_BIT | WIFI_CONNECTED_BIT, true, false, pdMS_TO_TICKS(5000));
		if (bits & WIFI_DISCONNECTED_BIT) {
			ESP_LOGW("wifi", "WiFi disconnected");
			esp_wifi_connect();
		} else if (bits & WIFI_CONNECTED_BIT) {
			ESP_LOGI("wifi", "WiFi reconnected");
		}
	}
}

static void mqtt_publish_result(esp_err_t err) {
	static const char *topic = MKTOPIC("lego/cmd/callback");
	const char *payload = NULL;
	switch (err) {
	case ESP_OK:
		payload = "done";
		break;
	case ESP_ERR_INVALID_ARG:
		payload = "invalid_arg";
		break;
	case ESP_ERR_TIMEOUT:
		payload = "timeout";
		break;
	case ESP_FAIL:
		payload = "fail";
		break;
	default:
		payload = "unknown_error";
		break;
	}
	esp_mqtt_client_publish(mqtt_handle, topic, payload, 0, 0, false);
}

#endif
