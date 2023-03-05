// SPDX-License-Identifier: Commercial
/*
 * Copyright (C) 2022 He Yong <hyyoxhk@163.com>
 */

#include "mqtt_client.h"
#include "cJSON.h"
#include "device.h"

int register_device(esp_mqtt_client_handle_t client, struct device *dev)
{
	uint32_t devId;
	char *data;
	cJSON *root;

	if (!dev->type)
		return -1;

	root = cJSON_CreateObject();
	if (!root)
		return -1;

	cJSON_AddItemToObject(root, "Type", cJSON_CreateString(dev->type));
	cJSON_AddItemToObject(root, "DevId", cJSON_CreateString(dev->dev_id));

	data = cJSON_Print(root);
	esp_mqtt_client_publish(client, "/topic/device", data, 0, 1, 0);

	cJSON_free(data);
	cJSON_Delete(root);

	return 0;
}
