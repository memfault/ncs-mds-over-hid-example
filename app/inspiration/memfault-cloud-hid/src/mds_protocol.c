/**
 * @file mds_protocol.c
 * @brief Implementation of Memfault Diagnostic Service over HID
 */

#include "memfault_hid/mds_protocol.h"
#include "memfault_hid/memfault_hid.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* MDS Session structure */
struct mds_session {
    memfault_hid_device_t *device;
    uint8_t last_sequence;
    bool streaming_enabled;

    /* Chunk upload */
    mds_chunk_upload_callback_t upload_callback;
    void *upload_user_data;
};

/* ============================================================================
 * MDS Session Management
 * ========================================================================== */

int mds_session_create(memfault_hid_device_t *device, mds_session_t **session) {
    if (session == NULL) {
        return -EINVAL;
    }

    /* Note: device can be NULL for FFI/external HID transport usage */

    mds_session_t *s = calloc(1, sizeof(mds_session_t));
    if (s == NULL) {
        return -ENOMEM;
    }

    s->device = device;
    s->last_sequence = MDS_SEQUENCE_MAX;  /* Initialize to max so first packet (0) is valid */
    s->streaming_enabled = false;

    *session = s;
    return 0;
}

void mds_session_destroy(mds_session_t *session) {
    if (session == NULL) {
        return;
    }

    /* Disable streaming if enabled */
    if (session->streaming_enabled) {
        mds_stream_disable(session);
    }

    free(session);
}

/* ============================================================================
 * Device Configuration
 * ========================================================================== */

int mds_read_device_config(mds_session_t *session, mds_device_config_t *config) {
    if (session == NULL || config == NULL) {
        return -EINVAL;
    }

    int ret;

    /* Read supported features */
    ret = mds_get_supported_features(session, &config->supported_features);
    if (ret < 0) {
        return ret;
    }

    /* Read device identifier */
    ret = mds_get_device_identifier(session, config->device_identifier,
                                     sizeof(config->device_identifier));
    if (ret < 0) {
        return ret;
    }

    /* Read data URI */
    ret = mds_get_data_uri(session, config->data_uri,
                          sizeof(config->data_uri));
    if (ret < 0) {
        return ret;
    }

    /* Read authorization */
    ret = mds_get_authorization(session, config->authorization,
                               sizeof(config->authorization));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int mds_get_supported_features(mds_session_t *session, uint32_t *features) {
    if (session == NULL || features == NULL) {
        return -EINVAL;
    }

    uint8_t data[4] = {0};
    int ret = memfault_hid_get_feature_report(session->device,
                                               MDS_REPORT_ID_SUPPORTED_FEATURES,
                                               data, sizeof(data));
    if (ret < 0) {
        return ret;
    }

    /* Use the buffer-based parser */
    return mds_parse_supported_features(data, ret, features);
}

int mds_get_device_identifier(mds_session_t *session, char *device_id, size_t max_len) {
    if (session == NULL || device_id == NULL || max_len == 0) {
        return -EINVAL;
    }

    uint8_t data[MDS_MAX_DEVICE_ID_LEN];
    int ret = memfault_hid_get_feature_report(session->device,
                                               MDS_REPORT_ID_DEVICE_IDENTIFIER,
                                               data, sizeof(data));
    if (ret < 0) {
        return ret;
    }

    /* Use the buffer-based parser */
    return mds_parse_device_identifier(data, ret, device_id, max_len);
}

int mds_get_data_uri(mds_session_t *session, char *uri, size_t max_len) {
    if (session == NULL || uri == NULL || max_len == 0) {
        return -EINVAL;
    }

    uint8_t data[MDS_MAX_URI_LEN];
    int ret = memfault_hid_get_feature_report(session->device,
                                               MDS_REPORT_ID_DATA_URI,
                                               data, sizeof(data));
    if (ret < 0) {
        return ret;
    }

    /* Use the buffer-based parser */
    return mds_parse_data_uri(data, ret, uri, max_len);
}

int mds_get_authorization(mds_session_t *session, char *auth, size_t max_len) {
    if (session == NULL || auth == NULL || max_len == 0) {
        return -EINVAL;
    }

    uint8_t data[MDS_MAX_AUTH_LEN];
    int ret = memfault_hid_get_feature_report(session->device,
                                               MDS_REPORT_ID_AUTHORIZATION,
                                               data, sizeof(data));
    if (ret < 0) {
        return ret;
    }

    /* Use the buffer-based parser */
    return mds_parse_authorization(data, ret, auth, max_len);
}

/* ============================================================================
 * Stream Control
 * ========================================================================== */

int mds_stream_enable(mds_session_t *session) {
    if (session == NULL) {
        return -EINVAL;
    }

    /* Use the buffer-based builder */
    uint8_t buffer[1];
    int bytes = mds_build_stream_control(true, buffer, sizeof(buffer));
    if (bytes < 0) {
        return bytes;
    }

    int ret = memfault_hid_write_report(session->device,
                                         MDS_REPORT_ID_STREAM_CONTROL,
                                         buffer, bytes, 1000);
    if (ret < 0) {
        return ret;
    }

    session->streaming_enabled = true;
    return 0;
}

int mds_stream_disable(mds_session_t *session) {
    if (session == NULL) {
        return -EINVAL;
    }

    /* Use the buffer-based builder */
    uint8_t buffer[1];
    int bytes = mds_build_stream_control(false, buffer, sizeof(buffer));
    if (bytes < 0) {
        return bytes;
    }

    int ret = memfault_hid_write_report(session->device,
                                         MDS_REPORT_ID_STREAM_CONTROL,
                                         buffer, bytes, 1000);
    if (ret < 0) {
        return ret;
    }

    session->streaming_enabled = false;
    return 0;
}

/* ============================================================================
 * Stream Data Reception
 * ========================================================================== */

int mds_stream_read_packet(mds_session_t *session, mds_stream_packet_t *packet,
                           int timeout_ms) {
    if (session == NULL || packet == NULL) {
        return -EINVAL;
    }

    uint8_t report_id;
    uint8_t data[MDS_MAX_CHUNK_DATA_LEN + 1];  /* +1 for sequence byte */

    int ret = memfault_hid_read_report(session->device, &report_id,
                                        data, sizeof(data), timeout_ms);
    if (ret < 0) {
        return ret;
    }

    /* Verify this is a stream data report */
    if (report_id != MDS_REPORT_ID_STREAM_DATA) {
        return -EINVAL;  /* Wrong report type */
    }

    /* Use the buffer-based parser */
    ret = mds_parse_stream_packet(data, ret, packet);
    if (ret < 0) {
        return ret;
    }

    /* Update last sequence */
    session->last_sequence = packet->sequence;

    return 0;
}

/* ============================================================================
 * Chunk Upload
 * ========================================================================== */

int mds_set_upload_callback(mds_session_t *session,
                             mds_chunk_upload_callback_t callback,
                             void *user_data) {
    if (session == NULL) {
        return -EINVAL;
    }

    session->upload_callback = callback;
    session->upload_user_data = user_data;

    return 0;
}

int mds_stream_process(mds_session_t *session,
                        const mds_device_config_t *config,
                        int timeout_ms) {
    if (session == NULL || config == NULL) {
        return -EINVAL;
    }

    mds_stream_packet_t packet;
    int ret = mds_stream_read_packet(session, &packet, timeout_ms);
    if (ret < 0) {
        return ret;
    }

    /* Validate sequence if we have a previous sequence */
    if (session->last_sequence != MDS_SEQUENCE_MAX) {
        if (!mds_validate_sequence(session->last_sequence, packet.sequence)) {
            /* Log warning but continue - sequence validation is not critical */
            /* Note: Actual logging would require a logging callback or printf */
        }
    }

    /* Upload chunk if callback is configured */
    if (session->upload_callback != NULL) {
        ret = session->upload_callback(config->data_uri,
                                        config->authorization,
                                        packet.data,
                                        packet.data_len,
                                        session->upload_user_data);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

bool mds_validate_sequence(uint8_t prev_seq, uint8_t new_seq) {
    /* Expected next sequence */
    uint8_t expected = (prev_seq + 1) & MDS_SEQUENCE_MASK;

    return (new_seq == expected);
}

/* ============================================================================
 * Buffer-based API for FFI/External HID Transport
 * ========================================================================== */

int mds_parse_supported_features(const uint8_t *buffer, size_t buffer_len,
                                  uint32_t *features) {
    if (buffer == NULL || features == NULL) {
        return -EINVAL;
    }

    if (buffer_len < 4) {
        return -EINVAL;
    }

    /* Features is stored as little-endian 32-bit value */
    *features = (uint32_t)buffer[0] |
                ((uint32_t)buffer[1] << 8) |
                ((uint32_t)buffer[2] << 16) |
                ((uint32_t)buffer[3] << 24);

    return 0;
}

int mds_parse_device_identifier(const uint8_t *buffer, size_t buffer_len,
                                 char *device_id, size_t max_len) {
    if (buffer == NULL || device_id == NULL || max_len == 0) {
        return -EINVAL;
    }

    /* Copy string, ensuring null termination */
    size_t copy_len = (buffer_len < max_len) ? buffer_len : (max_len - 1);
    memcpy(device_id, buffer, copy_len);
    device_id[copy_len] = '\0';

    return 0;
}

int mds_parse_data_uri(const uint8_t *buffer, size_t buffer_len,
                        char *uri, size_t max_len) {
    if (buffer == NULL || uri == NULL || max_len == 0) {
        return -EINVAL;
    }

    /* Copy string, ensuring null termination */
    size_t copy_len = (buffer_len < max_len) ? buffer_len : (max_len - 1);
    memcpy(uri, buffer, copy_len);
    uri[copy_len] = '\0';

    return 0;
}

int mds_parse_authorization(const uint8_t *buffer, size_t buffer_len,
                             char *auth, size_t max_len) {
    if (buffer == NULL || auth == NULL || max_len == 0) {
        return -EINVAL;
    }

    /* Copy string, ensuring null termination */
    size_t copy_len = (buffer_len < max_len) ? buffer_len : (max_len - 1);
    memcpy(auth, buffer, copy_len);
    auth[copy_len] = '\0';

    return 0;
}

int mds_build_stream_control(bool enable, uint8_t *buffer, size_t buffer_len) {
    if (buffer == NULL || buffer_len < 1) {
        return -EINVAL;
    }

    buffer[0] = enable ? MDS_STREAM_MODE_ENABLED : MDS_STREAM_MODE_DISABLED;
    return 1;
}

int mds_parse_stream_packet(const uint8_t *buffer, size_t buffer_len,
                             mds_stream_packet_t *packet) {
    if (buffer == NULL || packet == NULL) {
        return -EINVAL;
    }

    if (buffer_len < 1) {
        return -EINVAL;  /* Need at least sequence byte */
    }

    /* Extract sequence number */
    packet->sequence = mds_extract_sequence(buffer[0]);

    /* Copy payload data */
    packet->data_len = buffer_len - 1;  /* Exclude sequence byte */
    if (packet->data_len > MDS_MAX_CHUNK_DATA_LEN) {
        packet->data_len = MDS_MAX_CHUNK_DATA_LEN;
    }

    if (packet->data_len > 0) {
        memcpy(packet->data, &buffer[1], packet->data_len);
    }

    return 0;
}

uint8_t mds_get_last_sequence(mds_session_t *session) {
    if (session == NULL) {
        return 0;
    }
    return session->last_sequence;
}

void mds_update_last_sequence(mds_session_t *session, uint8_t sequence) {
    if (session != NULL) {
        session->last_sequence = sequence & MDS_SEQUENCE_MASK;
    }
}
