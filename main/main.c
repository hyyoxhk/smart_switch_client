// SPDX-License-Identifier: Commercial
/*
 * Copyright (C) 2022 He Yong <hyyoxhk@163.com>
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "smartconfig_ack.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"

#include "device.h"

#define ESP_SMARTCOFNIG_TYPE CONFIG_ESP_SMARTCONFIG_TYPE

#define GPIO_OUTPUT_IO_SWITCH_CTRL    0
#define GPIO_OUTPUT_IO_WIFI_STATUS    2
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_SWITCH_CTRL) | (1ULL<<GPIO_OUTPUT_IO_WIFI_STATUS))

static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;

static const char *TAG = "APP";

static struct device switch_dev;

static void smartconfig_task(void* parm)
{
	EventBits_t uxBits;
	ESP_ERROR_CHECK(esp_smartconfig_set_type(ESP_SMARTCOFNIG_TYPE));
	smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

	for (;;) {
		uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);

		if (uxBits & CONNECTED_BIT)
			ESP_LOGI(TAG, "WiFi Connected to ap");

		if (uxBits & ESPTOUCH_DONE_BIT) {
			ESP_LOGI(TAG, "smartconfig over");
			esp_smartconfig_stop();
			vTaskDelete(NULL);
		}
	}
}

static bool get_wifi_info_from_nvs(wifi_config_t *config)
{
	nvs_handle out_handle;
	esp_err_t err;
	size_t size;
	bool ret = true;

	err = nvs_open("wifi_info", NVS_READONLY, &out_handle);
	if (err != ESP_OK)
		return false;

	memset(config, 0x0, sizeof(wifi_config_t));

	size = sizeof(config->sta.ssid) + 1;
	err = nvs_get_str(out_handle, "ssid", (char *)config->sta.ssid, &size);
	if (err != ESP_OK) {
		ret = false;
		goto fail;
	}

	size = sizeof(config->sta.password) + 1;
	err = nvs_get_str(out_handle, "password", (char *)config->sta.password, &size);
	if (err != ESP_OK) {
		ret = false;
		goto fail;
	}

fail:
	nvs_close(out_handle);
	return ret;
}

static bool set_wifi_info_from_nvs(wifi_config_t *config)
{
	nvs_handle out_handle;
	esp_err_t err;
	bool ret = true;

	err = nvs_open("wifi_info", NVS_READWRITE, &out_handle);
	if (err != ESP_OK)
		return false;

	err = nvs_set_str(out_handle, "ssid", (const char *)config->sta.ssid);
	if (err != ESP_OK) {
		ret = false;
		goto fail;
	}

	err = nvs_set_str(out_handle, "password", (const char *)config->sta.password);
	if (err != ESP_OK) {
		ret = false;
		goto fail;
	}
fail:
	nvs_close(out_handle);
	return ret;
}

static void event_handler(void* arg, esp_event_base_t event_base, 
			  int32_t event_id, void *event_data)
{
	smartconfig_event_got_ssid_pswd_t *evt;
	wifi_config_t wifi_config;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		// esp_err_t err = esp_wifi_connect();
		// ESP_LOGI(TAG, "esp_wifi_connect err = %d", err);


		// if (err != ESP_OK) {
		// 	xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
		// 	ESP_LOGI(TAG, "xTaskCreate smartconfig_task");
		// }
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		esp_wifi_connect();
		xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		/* turning on the status light after wifi connection */
		gpio_set_level(GPIO_OUTPUT_IO_WIFI_STATUS, 0);

		ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
		xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
	} else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
		ESP_LOGI(TAG, "scan done");
	}else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
		ESP_LOGI(TAG, "found channel");
	} else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
		ESP_LOGI(TAG, "got SSID and password");

		evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

		memset(&wifi_config, 0, sizeof(wifi_config_t));
		memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
		memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

		if (wifi_config.sta.bssid_set == true) {
			memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
		}

		ESP_LOGI(TAG, "SSID:%s", wifi_config.sta.ssid);
		ESP_LOGI(TAG, "PASSWORD:%s", wifi_config.sta.password);

		if (set_wifi_info_from_nvs(&wifi_config))
			ESP_LOGI(TAG, "save SSID&PASSWORD to NVS");

		ESP_ERROR_CHECK(esp_wifi_disconnect());
		ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
		ESP_ERROR_CHECK(esp_wifi_connect());
	} else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
		xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
	}
}

static void initialise_wifi(void)
{
	wifi_config_t config;
	bool isget = false;

	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

	isget = get_wifi_info_from_nvs(&config);
	if (isget) {
		ESP_LOGI(TAG, "get ssid: %s", config.sta.ssid);
		ESP_LOGI(TAG, "get password: %s", config.sta.password);
		ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &config));

		ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
		ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
		esp_wifi_connect();
	} else {
		ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

		ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
		ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());	
		xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
	}
}

static void initialise_gpio(void)
{
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO15/16
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);
}

static int mqtt_event_data_handler(void *data, int len)
{
	char *value = strndup(data, len);
	if (!value)
		return -1;

	cJSON *pJsonRoot = cJSON_Parse(value);
	if (pJsonRoot == NULL) {
		free(value);
		return -1;
	}

	cJSON *type = cJSON_GetObjectItem(pJsonRoot, "Type");
	cJSON *devId = cJSON_GetObjectItem(pJsonRoot, "DevId");
	char *type_str = cJSON_GetStringValue(type);
	char *devId_str = cJSON_GetStringValue(devId);

	if (strcmp(type_str, "switch") == 0 && strcmp(devId_str, switch_dev.dev_id) == 0) {
		char *status = cJSON_GetStringValue(cJSON_GetObjectItem(pJsonRoot, "Status"));
		if (strcmp(status, "on") == 0)
			gpio_set_level(GPIO_OUTPUT_IO_SWITCH_CTRL, 0);
		if (strcmp(status, "off") == 0)
			gpio_set_level(GPIO_OUTPUT_IO_SWITCH_CTRL, 1);

	}

	cJSON_Delete(pJsonRoot);
	free(value);
	return 0;
}

static uint32_t get_chip_id(void)
{
	uint32_t chip_id;
	chip_id = (REG_READ(0x3FF00050) & 0xFF000000) | (REG_READ(0x3ff00054) & 0xFFFFFF);
	return chip_id;
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;

	switch (event->event_id) {
	case MQTT_EVENT_CONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

		switch_dev.type = "switch",
		snprintf(switch_dev.dev_id, sizeof(switch_dev.dev_id), "%02X", get_chip_id());

		if (register_device(client, &switch_dev) < 0)
			break;

		msg_id = esp_mqtt_client_subscribe(client, "/topic/switch", 1);
		ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		break;
	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
		ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "MQTT_EVENT_DATA");

		char *topic = strndup(event->topic, event->topic_len);
		if (strcmp(topic, "/topic/switch") == 0) {
			mqtt_event_data_handler(event->data, event->data_len);
		}
		free(topic);
		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		break;
	default:
		ESP_LOGI(TAG, "Other event id:%d", event->event_id);
		break;
	}
	return ESP_OK;
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
	mqtt_event_handler_cb(event_data);
}

void initialise_mqtt(void)
{
	esp_mqtt_client_config_t mqtt_cfg = {
		.uri = "mqtt://192.168.50.100",
	};

	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
	esp_mqtt_client_start(client);
}

void app_main(void)
{
	ESP_LOGI(TAG, "Startup..");
	/* Print chip information */
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	ESP_LOGI(TAG, "this is ESP8266 chip with %d CPU cores", chip_info.cores);
	ESP_LOGI(TAG, "revision %d, ", chip_info.revision);
	ESP_LOGI(TAG, "sdk version: %s", esp_get_idf_version());
	ESP_LOGI(TAG, "%dMB %s flash", spi_flash_get_chip_size() / (1024 * 1024),
		 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
	ESP_LOGI(TAG, "free memory: %d kB", esp_get_free_heap_size() / 1024);

	printf("ESP8266	chip	ID:0x%x\n", get_chip_id());

	/* Initialize NVS */
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	initialise_gpio();

	initialise_wifi();

	initialise_mqtt();

	//xTaskCreate(wifi_init_sta, "wifi_init_sta", 1024 * 10, NULL, 2, NULL);
}
