/*
 * Copyright (c) 2018 qianfan Zhao
 * Copyright (c) 2018, 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include <memfault/config.h>
#include <memfault/core/platform/device_info.h>
#include <memfault/core/data_packetizer.h>

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* MDS Protocol Report IDs */
#define MDS_REPORT_ID_SUPPORTED_FEATURES    0x01
#define MDS_REPORT_ID_DEVICE_IDENTIFIER     0x02
#define MDS_REPORT_ID_DATA_URI              0x03
#define MDS_REPORT_ID_AUTHORIZATION         0x04
#define MDS_REPORT_ID_STREAM_CONTROL        0x05
#define MDS_REPORT_ID_STREAM_DATA           0x06

/* MDS Protocol Constants */
#define MDS_MAX_DEVICE_ID_LEN               64
#define MDS_MAX_URI_LEN                     128
#define MDS_MAX_AUTH_LEN                    128
#define MDS_MAX_CHUNK_DATA_LEN              63
#define MDS_SEQUENCE_MASK                   0x1F

/* Stream control modes */
#define MDS_STREAM_MODE_DISABLED            0x00
#define MDS_STREAM_MODE_ENABLED             0x01

/* Supported features bitmask - all features supported */
static const uint32_t mds_supported_features = 0x0000001F;

/* HID Report Descriptor for MDS Protocol */
static const uint8_t hid_report_desc[] = {
	/* Usage Page (Vendor Defined) */
	0x06, 0x00, 0xFF,
	/* Usage (Vendor Defined) */
	0x09, 0x01,
	/* Collection (Application) */
	0xA1, 0x01,

	/* Feature Report: Supported Features (Report ID 0x01, 4 bytes) */
	0x85, MDS_REPORT_ID_SUPPORTED_FEATURES,
	0x09, 0x02,
	0x95, 0x04,  /* Report Count (4) */
	0x75, 0x08,  /* Report Size (8) */
	0x15, 0x00,  /* Logical Minimum (0) */
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
	0xB1, 0x02,  /* Feature (Data, Variable, Absolute) */

	/* Feature Report: Device Identifier (Report ID 0x02, 64 bytes) */
	0x85, MDS_REPORT_ID_DEVICE_IDENTIFIER,
	0x09, 0x03,
	0x95, MDS_MAX_DEVICE_ID_LEN,  /* Report Count (64) */
	0x75, 0x08,  /* Report Size (8) */
	0x15, 0x00,  /* Logical Minimum (0) */
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
	0xB1, 0x02,  /* Feature (Data, Variable, Absolute) */

	/* Feature Report: Data URI (Report ID 0x03, 128 bytes) */
	0x85, MDS_REPORT_ID_DATA_URI,
	0x09, 0x04,
	0x95, MDS_MAX_URI_LEN,  /* Report Count (128) */
	0x75, 0x08,  /* Report Size (8) */
	0x15, 0x00,  /* Logical Minimum (0) */
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
	0xB1, 0x02,  /* Feature (Data, Variable, Absolute) */

	/* Feature Report: Authorization (Report ID 0x04, 128 bytes) */
	0x85, MDS_REPORT_ID_AUTHORIZATION,
	0x09, 0x05,
	0x95, MDS_MAX_AUTH_LEN,  /* Report Count (128) */
	0x75, 0x08,  /* Report Size (8) */
	0x15, 0x00,  /* Logical Minimum (0) */
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
	0xB1, 0x02,  /* Feature (Data, Variable, Absolute) */

	/* Feature Report: Stream Control (Report ID 0x05, 1 byte) */
	0x85, MDS_REPORT_ID_STREAM_CONTROL,
	0x09, 0x06,
	0x95, 0x01,  /* Report Count (1) */
	0x75, 0x08,  /* Report Size (8) */
	0x15, 0x00,  /* Logical Minimum (0) */
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
	0xB1, 0x02,  /* Feature (Data, Variable, Absolute) */

	/* Input Report: Stream Data (Report ID 0x06, 64 bytes) */
	0x85, MDS_REPORT_ID_STREAM_DATA,
	0x09, 0x07,
	0x95, 0x40,  /* Report Count (64) */
	0x75, 0x08,  /* Report Size (8) */
	0x15, 0x00,  /* Logical Minimum (0) */
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */
	0x81, 0x02,  /* Input (Data, Variable, Absolute) */

	/* End Collection */
	0xC0,
};

/* MDS State */
struct mds_state {
	bool hid_ready;
	bool streaming_enabled;
	uint8_t chunk_number;
};

static struct mds_state mds = {
	.hid_ready = false,
	.streaming_enabled = false,
	.chunk_number = 0,
};

/* Memfault configuration strings */
#define MDS_URI_BASE \
	MEMFAULT_HTTP_APIS_DEFAULT_SCHEME "://" MEMFAULT_HTTP_CHUNKS_API_HOST "/api/v0/chunks/"

#define MDS_AUTH_KEY "Memfault-Project-Key: " CONFIG_MEMFAULT_NCS_PROJECT_KEY

static void mds_iface_ready(const struct device *dev, const bool ready)
{
	LOG_INF("HID device %s interface is %s",
		dev->name, ready ? "ready" : "not ready");
	mds.hid_ready = ready;
}

static int mds_get_report(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 uint8_t *const buf)
{
	LOG_INF("Get Report Type %u ID %u Len %u", type, id, len);

	/* Only handle Feature Reports */
	if (type != HID_REPORT_TYPE_FEATURE) {
		LOG_WRN("Unsupported report type %u", type);
		return -ENOTSUP;
	}

	/* Include Report ID as first byte */
	buf[0] = id;

	switch (id) {
	case MDS_REPORT_ID_SUPPORTED_FEATURES: {
		if (len < 5) {  /* Need 1 (Report ID) + 4 (data) */
			return -EINVAL;
		}
		/* Return supported features as little-endian uint32 */
		buf[1] = (mds_supported_features) & 0xFF;
		buf[2] = (mds_supported_features >> 8) & 0xFF;
		buf[3] = (mds_supported_features >> 16) & 0xFF;
		buf[4] = (mds_supported_features >> 24) & 0xFF;
		return 5;  /* 4 bytes payload + 1 Report ID = 5 total */
	}

	case MDS_REPORT_ID_DEVICE_IDENTIFIER: {
		sMemfaultDeviceInfo info;
		memfault_platform_get_device_info(&info);
		size_t device_id_len = strlen(info.device_serial);
		size_t copy_len = (device_id_len < (len - 1)) ? device_id_len : (len - 1);
		memcpy(&buf[1], info.device_serial, copy_len);  /* Data starts at buf[1] */
		return copy_len + 1;  /* payload + 1 for Report ID */
	}

	case MDS_REPORT_ID_DATA_URI: {
		sMemfaultDeviceInfo info;
		memfault_platform_get_device_info(&info);

		char uri[MDS_MAX_URI_LEN];
		size_t uri_base_len = strlen(MDS_URI_BASE);
		size_t uri_sn_len = strlen(info.device_serial);
		size_t uri_len = uri_base_len + uri_sn_len;

		if (uri_len > sizeof(uri)) {
			LOG_ERR("URI too long");
			return -EINVAL;
		}

		memcpy(uri, MDS_URI_BASE, uri_base_len);
		memcpy(&uri[uri_base_len], info.device_serial, uri_sn_len);

		size_t copy_len = (uri_len < (len - 1)) ? uri_len : (len - 1);
		memcpy(&buf[1], uri, copy_len);  /* Data starts at buf[1] */
		return copy_len + 1;  /* payload + 1 for Report ID */
	}

	case MDS_REPORT_ID_AUTHORIZATION: {
		const char *auth = MDS_AUTH_KEY;
		size_t auth_len = strlen(auth);
		size_t copy_len = (auth_len < (len - 1)) ? auth_len : (len - 1);
		memcpy(&buf[1], auth, copy_len);  /* Data starts at buf[1] */
		return copy_len + 1;  /* payload + 1 for Report ID */
	}

	default:
		LOG_WRN("Unknown report ID %u", id);
		return -ENOTSUP;
	}
}

static int mds_set_report(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 const uint8_t *const buf)
{
	LOG_INF("Set Report Type %u ID %u Len %u", type, id, len);

	/* Handle Feature Reports */
	if (type == HID_REPORT_TYPE_FEATURE) {
		switch (id) {
		case MDS_REPORT_ID_STREAM_CONTROL: {
			if (len < 2) {
				return -EINVAL;
			}

			/* buf[0] contains the Report ID, actual data starts at buf[1] */
			uint8_t mode = buf[1];
			LOG_INF("Stream control: %s",
				mode == MDS_STREAM_MODE_ENABLED ? "ENABLED" : "DISABLED");

			if (mode == MDS_STREAM_MODE_ENABLED) {
				mds.streaming_enabled = true;
			} else if (mode == MDS_STREAM_MODE_DISABLED) {
				mds.streaming_enabled = false;
				mds.chunk_number = 0;
			} else {
				LOG_WRN("Invalid stream mode %u", mode);
				return -EINVAL;
			}

			return 0;
		}

		default:
			LOG_WRN("Unsupported feature report ID for set: %u", id);
			return -ENOTSUP;
		}
	}

	if (type != HID_REPORT_TYPE_OUTPUT) {
		LOG_WRN("Unsupported report type %u", type);
		return -ENOTSUP;
	}

	/* Handle Output Reports (none currently defined for MDS) */
	LOG_WRN("Unknown output report ID %u", id);
	return -ENOTSUP;
}

struct hid_device_ops mds_ops = {
	.iface_ready = mds_iface_ready,
	.get_report = mds_get_report,
	.set_report = mds_set_report,
};

/* Send MDS stream data chunk */
static int mds_send_chunk(const struct device *hid_dev)
{
	uint8_t report[65];  /* Report ID (1) + sequence (1) + data (63) */
	size_t chunk_max_size = MDS_MAX_CHUNK_DATA_LEN;  /* 63 */
	size_t chunk_size = chunk_max_size;
	bool data_available;

	/* Get chunk from Memfault packetizer */
	data_available = memfault_packetizer_get_chunk(&report[2], &chunk_size);

	if (!data_available) {
		return 0;  /* No data available */
	}

	/* Set Report ID in first byte */
	report[0] = MDS_REPORT_ID_STREAM_DATA;  /* 0x06 */

	/* Set sequence number in second byte (bits 0-4) */
	report[1] = mds.chunk_number & MDS_SEQUENCE_MASK;

	/* Pad remaining bytes to 63 total data bytes (excluding Report ID) */
	if (chunk_size < 63) {
		memset(&report[chunk_size + 2], 0, 63 - chunk_size);
	}

	/* Submit 65 bytes: 1 (Report ID) + 1 (sequence) + 63 (data) */
	int ret = hid_device_submit_report(hid_dev, 65, report);
	if (ret) {
		memfault_packetizer_abort();
		LOG_ERR("Failed to send chunk, err %d", ret);
		return ret;
	}

	LOG_DBG("Sent chunk %d, size %zu", mds.chunk_number, chunk_size);

	/* Update chunk number (wraps at 31) */
	mds.chunk_number = (mds.chunk_number + 1) & MDS_SEQUENCE_MASK;

	return chunk_size;
}

int main(void)
{
	struct usbd_context *sample_usbd;
	const struct device *hid_dev;
	int ret;

	if (!gpio_is_ready_dt(&led0)) {
		LOG_ERR("LED device %s is not ready", led0.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure the LED pin, error: %d", ret);
		return 0;
	}

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID Device is not ready");
		return -EIO;
	}

	ret = hid_device_register(hid_dev,
				  hid_report_desc, sizeof(hid_report_desc),
				  &mds_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID Device, %d", ret);
		return ret;
	}

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
	while (true) {
		if (!mds.hid_ready) {
			LOG_DBG("USB HID device is not ready");
			k_sleep(K_MSEC(1000));
			continue;
		}

		if (!mds.streaming_enabled) {
			/* Streaming disabled, just wait */
			k_sleep(K_MSEC(100));
			continue;
		}

		/* Try to send a chunk */
		ret = mds_send_chunk(hid_dev);
		if (ret > 0) {
			/* Chunk sent successfully, toggle LED */
			(void)gpio_pin_toggle(led0.port, led0.pin);
			/* Small delay between chunks */
			k_sleep(K_MSEC(10));
		} else if (ret == 0) {
			/* No data available, check again later */
			k_sleep(K_MSEC(100));
		} else {
			/* Error sending chunk */
			LOG_ERR("Error sending chunk: %d", ret);
			k_sleep(K_MSEC(100));
		}
	}

	return 0;
}
