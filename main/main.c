// SPDX-License-Identifier: Commercial
/*
 * Copyright (C) 2022 He Yong <hyyoxhk@163.com>
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "smartconfig_ack.h"
#include "esp_log.h"

#define ESP_SMARTCOFNIG_TYPE CONFIG_ESP_SMARTCONFIG_TYPE

static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;

static const char *TAG = "APP";

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
		/* TODO: 打开WiFi指示灯, 控制gpio1 = 0*/
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

static void wifi_init_sta(void)
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

	/* Initialize NVS */
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);



	//xTaskCreate(wifi_init_sta, "wifi_init_sta", 1024 * 10, NULL, 2, NULL);
	wifi_init_sta();
}
