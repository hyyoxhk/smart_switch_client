// SPDX-License-Identifier: Commercial
/*
 * Copyright (C) 2022 He Yong <hyyoxhk@163.com>
 */

#ifndef _DEVICE_
#define _DEVICE_

#include "mqtt_client.h"

struct device {
	const char *type;
	char dev_id[10];
};

int register_device(esp_mqtt_client_handle_t client, struct device *dev);

#endif
