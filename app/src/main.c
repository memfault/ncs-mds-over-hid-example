/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <memfault/components.h>

#include "mds_hid.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static const struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios);
static const struct gpio_dt_spec button3 = GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios);
static struct gpio_callback button_cb_data;

/*
 * Demo fault functions - these function names will appear in Memfault traces
 * making for compelling real-world scenarios in demos.
 */

/* Button 0: Random fault for variety */
static void trigger_random_fault(void)
{
	uint32_t fault_type = sys_rand32_get() % 4;

	LOG_INF("Triggering random fault type %d", fault_type);

	switch (fault_type) {
	case 0:
		LOG_ERR("Assert failure!");
		MEMFAULT_ASSERT(0);
		break;
	case 1:
		LOG_ERR("NULL pointer dereference!");
		{
			volatile uint32_t *p = NULL;
			*p = 0xDEADBEEF;
		}
		break;
	case 2:
		LOG_ERR("Divide by zero!");
		{
			volatile int a = 1;
			volatile int b = 0;
			volatile int c = a / b;
			(void)c;
		}
		break;
	case 3:
		LOG_ERR("Unaligned access!");
		{
			volatile uint8_t buf[8] = {0};
			volatile uint32_t *p = (volatile uint32_t *)&buf[1];
			*p = 0xDEADBEEF;
		}
		break;
	}
}

/* Button 1: Simulates a sensor driver NULL pointer bug */
static void __attribute__((noinline)) sensor_data_processing_callback(void)
{
	LOG_ERR("Processing sensor data...");
	volatile uint32_t *sensor_buffer = NULL;
	/* Simulate reading from uninitialized sensor buffer pointer */
	*sensor_buffer = 0xDEADBEEF;
}

/* Button 2: Simulates a BLE stack state machine assertion */
static void __attribute__((noinline)) ble_connection_state_handler(void)
{
	LOG_ERR("BLE connection state change...");
	/* Simulate invalid state transition assertion */
	MEMFAULT_ASSERT_RECORD(0);
}

/* Button 3: Simulates a flash storage hard fault */
static void __attribute__((noinline)) flash_storage_write_operation(void)
{
	LOG_ERR("Writing to flash storage...");
	/* Simulate write to invalid/protected flash address causing hard fault */
	volatile uint32_t *bad_addr = (volatile uint32_t *)0xFFFFFFFF;
	*bad_addr = 0xDEADBEEF;
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (pins & BIT(button0.pin)) {
		LOG_WRN("Button 0: Random fault");
		trigger_random_fault();
	} else if (pins & BIT(button1.pin)) {
		LOG_WRN("Button 1: Sensor data processing fault");
		sensor_data_processing_callback();
	} else if (pins & BIT(button2.pin)) {
		LOG_WRN("Button 2: BLE connection state assert");
		ble_connection_state_handler();
	} else if (pins & BIT(button3.pin)) {
		LOG_WRN("Button 3: Flash storage hard fault");
		flash_storage_write_operation();
	}
}

int main(void)
{
	struct usbd_context *sample_usbd;
	const struct device *hid_dev;
	int ret;

	/* Initialize LED */
	if (!gpio_is_ready_dt(&led0)) {
		LOG_ERR("LED device %s is not ready", led0.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure the LED pin, error: %d", ret);
		return 0;
	}

	/* Initialize all 4 buttons for demo fault triggers */
	const struct gpio_dt_spec *buttons[] = {&button0, &button1, &button2, &button3};
	uint32_t button_mask = 0;

	for (int i = 0; i < 4; i++) {
		if (!gpio_is_ready_dt(buttons[i])) {
			LOG_ERR("Button %d device not ready", i);
			return 0;
		}

		ret = gpio_pin_configure_dt(buttons[i], GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("Failed to configure button %d, error: %d", i, ret);
			return 0;
		}

		ret = gpio_pin_interrupt_configure_dt(buttons[i], GPIO_INT_EDGE_TO_ACTIVE);
		if (ret != 0) {
			LOG_ERR("Failed to configure button %d interrupt, error: %d", i, ret);
			return 0;
		}

		button_mask |= BIT(buttons[i]->pin);
	}

	gpio_init_callback(&button_cb_data, button_pressed, button_mask);
	gpio_add_callback(button0.port, &button_cb_data);

	LOG_INF("Demo buttons configured:");
	LOG_INF("  BTN0: Random fault");
	LOG_INF("  BTN1: sensor_data_processing_callback (NULL ptr)");
	LOG_INF("  BTN2: ble_connection_state_handler (assert)");
	LOG_INF("  BTN3: flash_storage_write_operation (hard fault)");

	/* Get HID device */
	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID Device is not ready");
		return -EIO;
	}

	/* Initialize MDS HID interface */
	ret = mds_hid_init(hid_dev);
	if (ret != 0) {
		LOG_ERR("Failed to initialize MDS HID, %d", ret);
		return ret;
	}

	/* Initialize and enable USB device */
	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	ret = usbd_enable(sample_usbd);
	if (ret != 0) {
		LOG_ERR("Failed to enable device support");
		return ret;
	}

	LOG_INF("MDS over HID device enabled");

	/* Main loop: Send diagnostic chunks when streaming is enabled */
	int chunks_sent = 0;

	while (true) {
		if (!mds_hid_is_ready()) {
			LOG_DBG("USB HID device is not ready");
			k_sleep(K_MSEC(1000));
			continue;
		}

		if (!mds_hid_is_streaming()) {
			/* Streaming disabled, just wait */
			k_sleep(K_MSEC(100));
			continue;
		}

		/* Try to send a chunk */
		ret = mds_hid_send_chunk(hid_dev);
		if (ret > 0) {
			chunks_sent++;
			/* Chunk sent successfully, toggle LED */
			(void)gpio_pin_toggle(led0.port, led0.pin);
			/* Small delay between chunks */
			k_sleep(K_MSEC(10));
		} else if (ret == 0) {
			/* No data available */
			if (chunks_sent > 0) {
				LOG_INF("Sent %d chunks", chunks_sent);
				chunks_sent = 0;
			}
			k_sleep(K_MSEC(100));
		} else {
			/* Error sending chunk */
			LOG_ERR("Error sending chunk: %d", ret);
			k_sleep(K_MSEC(100));
		}
	}

	return 0;
}
