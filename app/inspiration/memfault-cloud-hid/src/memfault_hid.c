/**
 * @file memfault_hid.c
 * @brief Main implementation of the memfault HID library
 */

#include "memfault_hid/memfault_hid.h"
#include <stdlib.h>
#include <string.h>
#include <hidapi.h>

/* Device structure */
struct memfault_hid_device {
    hid_device *handle;
    memfault_hid_device_info_t info;
    memfault_hid_report_filter_t filter;
    bool nonblocking;
};

/* Library initialization state */
static bool g_initialized = false;

/* ============================================================================
 * Library Initialization
 * ========================================================================== */

int memfault_hid_init(void) {
    if (g_initialized) {
        return MEMFAULT_HID_SUCCESS;
    }

    if (hid_init() != 0) {
        return MEMFAULT_HID_ERROR_UNKNOWN;
    }

    g_initialized = true;
    return MEMFAULT_HID_SUCCESS;
}

int memfault_hid_exit(void) {
    if (!g_initialized) {
        return MEMFAULT_HID_SUCCESS;
    }

    if (hid_exit() != 0) {
        return MEMFAULT_HID_ERROR_UNKNOWN;
    }

    g_initialized = false;
    return MEMFAULT_HID_SUCCESS;
}

/* ============================================================================
 * Device Enumeration
 * ========================================================================== */

int memfault_hid_enumerate(uint16_t vendor_id,
                           uint16_t product_id,
                           memfault_hid_device_info_t **devices,
                           size_t *num_devices) {
    if (!g_initialized) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    if (devices == NULL || num_devices == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    struct hid_device_info *dev_list = hid_enumerate(vendor_id, product_id);
    if (dev_list == NULL) {
        *devices = NULL;
        *num_devices = 0;
        return MEMFAULT_HID_SUCCESS;
    }

    /* Count devices */
    size_t count = 0;
    struct hid_device_info *cur = dev_list;
    while (cur) {
        count++;
        cur = cur->next;
    }

    /* Allocate device info array */
    memfault_hid_device_info_t *dev_array = calloc(count, sizeof(memfault_hid_device_info_t));
    if (dev_array == NULL) {
        hid_free_enumeration(dev_list);
        return MEMFAULT_HID_ERROR_NO_MEM;
    }

    /* Fill device info */
    cur = dev_list;
    for (size_t i = 0; i < count; i++) {
        strncpy(dev_array[i].path, cur->path, sizeof(dev_array[i].path) - 1);
        dev_array[i].vendor_id = cur->vendor_id;
        dev_array[i].product_id = cur->product_id;

        if (cur->serial_number) {
            wcsncpy(dev_array[i].serial_number, cur->serial_number, 127);
        }

        dev_array[i].release_number = cur->release_number;

        if (cur->manufacturer_string) {
            wcsncpy(dev_array[i].manufacturer, cur->manufacturer_string, 127);
        }

        if (cur->product_string) {
            wcsncpy(dev_array[i].product, cur->product_string, 127);
        }

        dev_array[i].usage_page = cur->usage_page;
        dev_array[i].usage = cur->usage;
        dev_array[i].interface_number = cur->interface_number;

        cur = cur->next;
    }

    hid_free_enumeration(dev_list);

    *devices = dev_array;
    *num_devices = count;
    return MEMFAULT_HID_SUCCESS;
}

void memfault_hid_free_device_list(memfault_hid_device_info_t *devices) {
    free(devices);
}

/* ============================================================================
 * Device Management
 * ========================================================================== */

int memfault_hid_open_path(const char *path, memfault_hid_device_t **device) {
    if (!g_initialized || path == NULL || device == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    memfault_hid_device_t *dev = calloc(1, sizeof(memfault_hid_device_t));
    if (dev == NULL) {
        return MEMFAULT_HID_ERROR_NO_MEM;
    }

    dev->handle = hid_open_path(path);
    if (dev->handle == NULL) {
        free(dev);
        return MEMFAULT_HID_ERROR_NOT_FOUND;
    }

    /* Store device path */
    strncpy(dev->info.path, path, sizeof(dev->info.path) - 1);

    dev->nonblocking = false;
    dev->filter.filter_enabled = false;

    *device = dev;
    return MEMFAULT_HID_SUCCESS;
}

int memfault_hid_open(uint16_t vendor_id,
                      uint16_t product_id,
                      const wchar_t *serial_number,
                      memfault_hid_device_t **device) {
    if (!g_initialized || device == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    memfault_hid_device_t *dev = calloc(1, sizeof(memfault_hid_device_t));
    if (dev == NULL) {
        return MEMFAULT_HID_ERROR_NO_MEM;
    }

    dev->handle = hid_open(vendor_id, product_id, serial_number);
    if (dev->handle == NULL) {
        free(dev);
        return MEMFAULT_HID_ERROR_NOT_FOUND;
    }

    /* Store basic device info */
    dev->info.vendor_id = vendor_id;
    dev->info.product_id = product_id;
    if (serial_number) {
        wcsncpy(dev->info.serial_number, serial_number, 127);
    }

    dev->nonblocking = false;
    dev->filter.filter_enabled = false;

    *device = dev;
    return MEMFAULT_HID_SUCCESS;
}

void memfault_hid_close(memfault_hid_device_t *device) {
    if (device == NULL) {
        return;
    }

    if (device->handle) {
        hid_close(device->handle);
    }

    if (device->filter.report_ids) {
        free(device->filter.report_ids);
    }

    free(device);
}

int memfault_hid_get_device_info(memfault_hid_device_t *device,
                                  memfault_hid_device_info_t *info) {
    if (device == NULL || info == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    memcpy(info, &device->info, sizeof(memfault_hid_device_info_t));
    return MEMFAULT_HID_SUCCESS;
}

/* ============================================================================
 * Report Filtering
 * ========================================================================== */

int memfault_hid_set_report_filter(memfault_hid_device_t *device,
                                    const memfault_hid_report_filter_t *filter) {
    if (device == NULL || filter == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    /* Free existing filter */
    if (device->filter.report_ids) {
        free(device->filter.report_ids);
        device->filter.report_ids = NULL;
    }

    /* Copy new filter */
    if (filter->num_report_ids > 0 && filter->report_ids != NULL) {
        device->filter.report_ids = malloc(filter->num_report_ids);
        if (device->filter.report_ids == NULL) {
            return MEMFAULT_HID_ERROR_NO_MEM;
        }
        memcpy(device->filter.report_ids, filter->report_ids, filter->num_report_ids);
    }

    device->filter.num_report_ids = filter->num_report_ids;
    device->filter.filter_enabled = filter->filter_enabled;

    return MEMFAULT_HID_SUCCESS;
}

int memfault_hid_get_report_filter(memfault_hid_device_t *device,
                                    memfault_hid_report_filter_t *filter) {
    if (device == NULL || filter == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    filter->report_ids = device->filter.report_ids;
    filter->num_report_ids = device->filter.num_report_ids;
    filter->filter_enabled = device->filter.filter_enabled;

    return MEMFAULT_HID_SUCCESS;
}

/* ============================================================================
 * Report Communication
 * ========================================================================== */

static bool is_report_filtered(memfault_hid_device_t *device, uint8_t report_id) {
    if (!device->filter.filter_enabled) {
        return false;
    }

    for (size_t i = 0; i < device->filter.num_report_ids; i++) {
        if (device->filter.report_ids[i] == report_id) {
            return false;  /* Report ID is in filter list, don't filter it */
        }
    }

    return true;  /* Report ID not in filter list, filter it out */
}

int memfault_hid_write_report(memfault_hid_device_t *device,
                               uint8_t report_id,
                               const uint8_t *data,
                               size_t length,
                               int timeout_ms) {
    if (device == NULL || data == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    if (is_report_filtered(device, report_id)) {
        return MEMFAULT_HID_ERROR_INVALID_REPORT_TYPE;
    }

    (void)timeout_ms;  /* hidapi doesn't support write timeout */

    /* Prepare buffer with Report ID */
    uint8_t buffer[MEMFAULT_HID_MAX_REPORT_SIZE + 1];
    buffer[0] = report_id;
    memcpy(buffer + 1, data, length);

    int result = hid_write(device->handle, buffer, length + 1);
    if (result < 0) {
        return MEMFAULT_HID_ERROR_IO;
    }

    return result - 1;  /* Don't count the Report ID byte */
}

int memfault_hid_read_report(memfault_hid_device_t *device,
                              uint8_t *report_id,
                              uint8_t *data,
                              size_t length,
                              int timeout_ms) {
    if (device == NULL || data == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    uint8_t buffer[MEMFAULT_HID_MAX_REPORT_SIZE + 1];
    int result;

    if (timeout_ms == 0) {
        result = hid_read(device->handle, buffer, sizeof(buffer));
    } else {
        result = hid_read_timeout(device->handle, buffer, sizeof(buffer), timeout_ms);
    }

    if (result < 0) {
        return MEMFAULT_HID_ERROR_IO;
    }

    if (result == 0) {
        return MEMFAULT_HID_ERROR_TIMEOUT;
    }

    /* First byte is Report ID */
    uint8_t rid = buffer[0];

    if (is_report_filtered(device, rid)) {
        return MEMFAULT_HID_ERROR_INVALID_REPORT_TYPE;
    }

    if (report_id) {
        *report_id = rid;
    }

    /* Copy data (excluding Report ID) */
    size_t data_len = (size_t)(result - 1);
    if (data_len > length) {
        data_len = length;
    }
    memcpy(data, buffer + 1, data_len);

    return (int)data_len;
}

int memfault_hid_get_feature_report(memfault_hid_device_t *device,
                                     uint8_t report_id,
                                     uint8_t *data,
                                     size_t length) {
    if (device == NULL || data == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    if (is_report_filtered(device, report_id)) {
        return MEMFAULT_HID_ERROR_INVALID_REPORT_TYPE;
    }

    uint8_t buffer[MEMFAULT_HID_MAX_REPORT_SIZE + 1];
    buffer[0] = report_id;

    int result = hid_get_feature_report(device->handle, buffer, length + 1);
    if (result < 0) {
        return MEMFAULT_HID_ERROR_IO;
    }

    /* Copy data (excluding Report ID) */
    memcpy(data, buffer + 1, result - 1);
    return result - 1;
}

int memfault_hid_set_feature_report(memfault_hid_device_t *device,
                                     uint8_t report_id,
                                     const uint8_t *data,
                                     size_t length) {
    if (device == NULL || data == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    if (is_report_filtered(device, report_id)) {
        return MEMFAULT_HID_ERROR_INVALID_REPORT_TYPE;
    }

    uint8_t buffer[MEMFAULT_HID_MAX_REPORT_SIZE + 1];
    buffer[0] = report_id;
    memcpy(buffer + 1, data, length);

    int result = hid_send_feature_report(device->handle, buffer, length + 1);
    if (result < 0) {
        return MEMFAULT_HID_ERROR_IO;
    }

    return result - 1;
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

const char *memfault_hid_error_string(int error) {
    switch (error) {
        case MEMFAULT_HID_SUCCESS:
            return "Success";
        case MEMFAULT_HID_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case MEMFAULT_HID_ERROR_NOT_FOUND:
            return "Device not found";
        case MEMFAULT_HID_ERROR_NO_DEVICE:
            return "No device";
        case MEMFAULT_HID_ERROR_ACCESS_DENIED:
            return "Access denied";
        case MEMFAULT_HID_ERROR_IO:
            return "I/O error";
        case MEMFAULT_HID_ERROR_TIMEOUT:
            return "Timeout";
        case MEMFAULT_HID_ERROR_BUSY:
            return "Device busy";
        case MEMFAULT_HID_ERROR_NO_MEM:
            return "Out of memory";
        case MEMFAULT_HID_ERROR_NOT_SUPPORTED:
            return "Not supported";
        case MEMFAULT_HID_ERROR_ALREADY_OPEN:
            return "Device already open";
        case MEMFAULT_HID_ERROR_INVALID_REPORT_TYPE:
            return "Invalid or filtered report type";
        default:
            return "Unknown error";
    }
}

const char *memfault_hid_version_string(void) {
    return "1.0.0";
}

int memfault_hid_set_nonblocking(memfault_hid_device_t *device, bool nonblock) {
    if (device == NULL) {
        return MEMFAULT_HID_ERROR_INVALID_PARAM;
    }

    int result = hid_set_nonblocking(device->handle, nonblock ? 1 : 0);
    if (result < 0) {
        return MEMFAULT_HID_ERROR_IO;
    }
    device->nonblocking = nonblock;
    return MEMFAULT_HID_SUCCESS;
}
