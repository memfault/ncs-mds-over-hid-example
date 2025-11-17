/**
 * @file mds_protocol.h
 * @brief Memfault Diagnostic Service (MDS) over HID
 *
 * This implements the Memfault Diagnostic Service protocol over HID, adapted from
 * the BLE GATT service specification. It provides a bridge for diagnostic data
 * from embedded devices to gateway applications.
 *
 * Protocol Overview:
 * - Feature reports provide device information and configuration
 * - Output reports control data streaming
 * - Input reports deliver diagnostic chunk data
 */

#ifndef MEMFAULT_MDS_PROTOCOL_H
#define MEMFAULT_MDS_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declaration - the typedef is in memfault_hid.h */
#ifndef MEMFAULT_HID_DEVICE_T_DEFINED
#define MEMFAULT_HID_DEVICE_T_DEFINED
typedef struct memfault_hid_device memfault_hid_device_t;
#endif

/* ============================================================================
 * Report ID Definitions
 * ========================================================================== */

/** Feature Report: Supported features bitmask (currently 0x00) */
#define MDS_REPORT_ID_SUPPORTED_FEATURES    0x01

/** Feature Report: Device identifier string */
#define MDS_REPORT_ID_DEVICE_IDENTIFIER     0x02

/** Feature Report: Data URI for uploading chunks */
#define MDS_REPORT_ID_DATA_URI              0x03

/** Feature Report: Authorization header (e.g., project key) */
#define MDS_REPORT_ID_AUTHORIZATION         0x04

/** Output Report: Stream control (enable/disable) */
#define MDS_REPORT_ID_STREAM_CONTROL        0x05

/** Input Report: Stream data packets (chunk data) */
#define MDS_REPORT_ID_STREAM_DATA           0x06

/* ============================================================================
 * Constants
 * ========================================================================== */

/** Maximum device identifier length */
#define MDS_MAX_DEVICE_ID_LEN               64

/** Maximum URI length */
#define MDS_MAX_URI_LEN                     128

/** Maximum authorization header length */
#define MDS_MAX_AUTH_LEN                    128

/** Maximum chunk data per packet (after sequence byte) */
#define MDS_MAX_CHUNK_DATA_LEN              63

/* ============================================================================
 * Stream Control Modes
 * ========================================================================== */

/** Stream control mode: Streaming disabled */
#define MDS_STREAM_MODE_DISABLED            0x00

/** Stream control mode: Streaming enabled */
#define MDS_STREAM_MODE_ENABLED             0x01

/* ============================================================================
 * Stream Data Packet Format
 * ========================================================================== */

/** Sequence counter mask (bits 0-4 of byte 0) */
#define MDS_SEQUENCE_MASK                   0x1F

/** Sequence counter max value (wraps at 31) */
#define MDS_SEQUENCE_MAX                    31

/* ============================================================================
 * Data Structures
 * ========================================================================== */

/**
 * @brief MDS device configuration
 *
 * This structure contains the device information and credentials
 * used for diagnostic data upload.
 */
typedef struct {
    /** Supported features bitmask (currently always 0x00) */
    uint32_t supported_features;

    /** Device identifier (null-terminated string) */
    char device_identifier[MDS_MAX_DEVICE_ID_LEN];

    /** Data URI for chunk upload (null-terminated string) */
    char data_uri[MDS_MAX_URI_LEN];

    /** Authorization header (null-terminated string) */
    char authorization[MDS_MAX_AUTH_LEN];
} mds_device_config_t;

/**
 * @brief MDS stream data packet
 *
 * Packet format for diagnostic chunk data.
 * Byte 0: Sequence counter (bits 0-4) + reserved (bits 5-7)
 * Byte 1+: Chunk data payload
 */
typedef struct {
    /** Sequence counter (0-31, wraps around) */
    uint8_t sequence;

    /** Chunk data payload */
    uint8_t data[MDS_MAX_CHUNK_DATA_LEN];

    /** Length of valid data in the data array */
    size_t data_len;
} mds_stream_packet_t;

/**
 * @brief Callback for uploading chunk data to the cloud
 *
 * This callback is invoked for each received chunk packet. The implementation
 * should POST the chunk data to the Memfault cloud.
 *
 * Expected HTTP request:
 * - Method: POST
 * - URL: uri parameter
 * - Headers:
 *   - Authorization header from auth_header (format: "HeaderName:HeaderValue")
 *   - Content-Type: application/octet-stream
 * - Body: chunk_data (chunk_len bytes)
 *
 * @param uri Data URI to upload to (from device config)
 * @param auth_header Authorization header (format: "HeaderName:HeaderValue")
 * @param chunk_data Chunk data bytes to upload
 * @param chunk_len Length of chunk data
 * @param user_data User-provided context pointer
 *
 * @return 0 on success, negative error code on failure
 */
typedef int (*mds_chunk_upload_callback_t)(const char *uri,
                                            const char *auth_header,
                                            const uint8_t *chunk_data,
                                            size_t chunk_len,
                                            void *user_data);

/* ============================================================================
 * MDS Session Management
 * ========================================================================== */

/**
 * @brief Opaque handle to an MDS session
 */
typedef struct mds_session mds_session_t;

/**
 * @brief Create an MDS session over a HID device
 *
 * @param device HID device handle (must be already opened)
 * @param session Pointer to receive session handle
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_session_create(memfault_hid_device_t *device,
                       mds_session_t **session);

/**
 * @brief Destroy an MDS session
 *
 * @param session Session handle to destroy
 */
void mds_session_destroy(mds_session_t *session);

/* ============================================================================
 * Device Configuration
 * ========================================================================== */

/**
 * @brief Read device configuration from the device
 *
 * Reads the supported features, device identifier, data URI, and
 * authorization header from the device using feature reports.
 *
 * @param session MDS session handle
 * @param config Pointer to receive device configuration
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_read_device_config(mds_session_t *session,
                           mds_device_config_t *config);

/**
 * @brief Get supported features
 *
 * @param session MDS session handle
 * @param features Pointer to receive features bitmask
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_get_supported_features(mds_session_t *session,
                               uint32_t *features);

/**
 * @brief Get device identifier
 *
 * @param session MDS session handle
 * @param device_id Buffer to receive device identifier (null-terminated)
 * @param max_len Maximum length of buffer
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_get_device_identifier(mds_session_t *session,
                              char *device_id,
                              size_t max_len);

/**
 * @brief Get data URI
 *
 * @param session MDS session handle
 * @param uri Buffer to receive URI (null-terminated)
 * @param max_len Maximum length of buffer
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_get_data_uri(mds_session_t *session,
                     char *uri,
                     size_t max_len);

/**
 * @brief Get authorization header
 *
 * @param session MDS session handle
 * @param auth Buffer to receive authorization (null-terminated)
 * @param max_len Maximum length of buffer
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_get_authorization(mds_session_t *session,
                         char *auth,
                         size_t max_len);

/* ============================================================================
 * Stream Control
 * ========================================================================== */

/**
 * @brief Enable diagnostic data streaming
 *
 * Sends a stream control output report to enable streaming.
 * After enabling, the device will begin sending chunk data via input reports.
 *
 * @param session MDS session handle
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_stream_enable(mds_session_t *session);

/**
 * @brief Disable diagnostic data streaming
 *
 * Sends a stream control output report to disable streaming.
 *
 * @param session MDS session handle
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_stream_disable(mds_session_t *session);

/* ============================================================================
 * Stream Data Reception
 * ========================================================================== */

/**
 * @brief Read a stream data packet
 *
 * Reads a diagnostic chunk data packet from the device.
 * This is a blocking call with the specified timeout.
 *
 * @param session MDS session handle
 * @param packet Pointer to receive stream packet
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 *
 * @return 0 on success, negative error code otherwise
 *         -ETIMEDOUT if no data available within timeout
 */
int mds_stream_read_packet(mds_session_t *session,
                           mds_stream_packet_t *packet,
                           int timeout_ms);

/* ============================================================================
 * Chunk Upload
 * ========================================================================== */

/**
 * @brief Set chunk upload callback
 *
 * Registers a callback that will be invoked to upload each received chunk.
 * This enables automatic chunk forwarding to the Memfault cloud when using
 * mds_stream_process().
 *
 * @param session MDS session handle
 * @param callback Upload callback function (NULL to disable)
 * @param user_data User context pointer passed to callback
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_set_upload_callback(mds_session_t *session,
                             mds_chunk_upload_callback_t callback,
                             void *user_data);

/**
 * @brief Process stream packets with automatic upload
 *
 * Reads stream packets and automatically uploads them using the configured
 * upload callback. This is a convenience function that combines packet reading,
 * sequence validation, and chunk uploading.
 *
 * Call this in a loop after enabling streaming. It will:
 * 1. Read a packet from the stream
 * 2. Validate the sequence number (logs warning if invalid)
 * 3. Upload the chunk via the callback (if configured)
 *
 * @param session MDS session handle
 * @param config Device configuration (contains URI and auth)
 * @param timeout_ms Timeout in milliseconds for reading packets
 *
 * @return 0 on success, negative error code otherwise
 *         -ETIMEDOUT if no data available within timeout
 *         Returns upload callback error code if upload fails
 */
int mds_stream_process(mds_session_t *session,
                        const mds_device_config_t *config,
                        int timeout_ms);

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Validate sequence number
 *
 * Checks if the sequence number is valid and detects dropped or duplicate packets.
 *
 * @param prev_seq Previous sequence number
 * @param new_seq New sequence number
 *
 * @return true if sequence is valid (next in sequence)
 *         false if packet was dropped or duplicated
 */
bool mds_validate_sequence(uint8_t prev_seq, uint8_t new_seq);

/**
 * @brief Extract sequence number from packet byte 0
 *
 * @param byte0 First byte of stream packet
 *
 * @return Sequence number (0-31)
 */
static inline uint8_t mds_extract_sequence(uint8_t byte0) {
    return byte0 & MDS_SEQUENCE_MASK;
}

/* ============================================================================
 * Buffer-based API for FFI/External HID Transport
 * ========================================================================== */

/**
 * @brief Parse supported features from feature report buffer
 *
 * Use this when your language/runtime handles HID I/O directly.
 *
 * @param buffer Feature report data (without Report ID prefix)
 * @param buffer_len Length of buffer
 * @param features Pointer to receive features bitmask
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_parse_supported_features(const uint8_t *buffer, size_t buffer_len,
                                  uint32_t *features);

/**
 * @brief Parse device identifier from feature report buffer
 *
 * @param buffer Feature report data (without Report ID prefix)
 * @param buffer_len Length of buffer
 * @param device_id Buffer to receive device ID (null-terminated)
 * @param max_len Maximum length of output buffer
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_parse_device_identifier(const uint8_t *buffer, size_t buffer_len,
                                 char *device_id, size_t max_len);

/**
 * @brief Parse data URI from feature report buffer
 *
 * @param buffer Feature report data (without Report ID prefix)
 * @param buffer_len Length of buffer
 * @param uri Buffer to receive URI (null-terminated)
 * @param max_len Maximum length of output buffer
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_parse_data_uri(const uint8_t *buffer, size_t buffer_len,
                        char *uri, size_t max_len);

/**
 * @brief Parse authorization from feature report buffer
 *
 * @param buffer Feature report data (without Report ID prefix)
 * @param buffer_len Length of buffer
 * @param auth Buffer to receive authorization (null-terminated)
 * @param max_len Maximum length of output buffer
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_parse_authorization(const uint8_t *buffer, size_t buffer_len,
                             char *auth, size_t max_len);

/**
 * @brief Build stream control output report
 *
 * Creates output report data for enabling/disabling streaming.
 *
 * @param enable true to enable streaming, false to disable
 * @param buffer Buffer to receive output report data (without Report ID prefix)
 * @param buffer_len Length of buffer (should be at least 1)
 *
 * @return Number of bytes written, or negative error code
 */
int mds_build_stream_control(bool enable, uint8_t *buffer, size_t buffer_len);

/**
 * @brief Parse stream data packet from input report buffer
 *
 * Extracts sequence number and chunk data from a stream data input report.
 *
 * @param buffer Input report data (without Report ID prefix)
 * @param buffer_len Length of buffer
 * @param packet Pointer to receive parsed packet
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_parse_stream_packet(const uint8_t *buffer, size_t buffer_len,
                             mds_stream_packet_t *packet);

/**
 * @brief Get last sequence number from session
 *
 * Useful for sequence tracking when using buffer-based API.
 *
 * @param session MDS session handle
 *
 * @return Last received sequence number (0-31)
 */
uint8_t mds_get_last_sequence(mds_session_t *session);

/**
 * @brief Update last sequence number in session
 *
 * Call this after successfully processing a packet when using buffer-based API.
 *
 * @param session MDS session handle
 * @param sequence New sequence number to store
 */
void mds_update_last_sequence(mds_session_t *session, uint8_t sequence);

#ifdef __cplusplus
}
#endif

#endif /* MEMFAULT_MDS_PROTOCOL_H */
