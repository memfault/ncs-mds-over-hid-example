/**
 * @file mds_upload.h
 * @brief Optional HTTP upload implementation for MDS chunks
 *
 * This header provides a ready-to-use HTTP uploader built on libcurl.
 * This is optional and only available if BUILD_MDS_UPLOAD is enabled.
 *
 * Usage:
 * 1. Create an uploader: mds_uploader_t *uploader = mds_uploader_create();
 * 2. Set it on the session: mds_set_upload_callback(session, mds_uploader_callback, uploader);
 * 3. Process streams: mds_stream_process(session, &config, timeout);
 * 4. Destroy when done: mds_uploader_destroy(uploader);
 */

#ifndef MEMFAULT_MDS_UPLOAD_H
#define MEMFAULT_MDS_UPLOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Opaque handle to an HTTP uploader
 */
typedef struct mds_uploader mds_uploader_t;

/**
 * @brief Upload statistics
 */
typedef struct {
    /** Total chunks uploaded successfully */
    size_t chunks_uploaded;

    /** Total bytes uploaded */
    size_t bytes_uploaded;

    /** Number of upload failures */
    size_t upload_failures;

    /** Last HTTP status code */
    long last_http_status;
} mds_upload_stats_t;

/**
 * @brief Create an HTTP uploader
 *
 * Creates an uploader instance using libcurl for HTTP POST requests.
 * The uploader can be used as a callback with mds_set_upload_callback().
 *
 * @return Uploader handle, or NULL on failure
 */
mds_uploader_t *mds_uploader_create(void);

/**
 * @brief Destroy an HTTP uploader
 *
 * Frees all resources associated with the uploader.
 *
 * @param uploader Uploader handle to destroy
 */
void mds_uploader_destroy(mds_uploader_t *uploader);

/**
 * @brief Upload callback for use with mds_set_upload_callback()
 *
 * This function can be passed directly to mds_set_upload_callback().
 * The user_data parameter should be a mds_uploader_t* instance.
 *
 * Example:
 *   mds_uploader_t *uploader = mds_uploader_create();
 *   mds_set_upload_callback(session, mds_uploader_callback, uploader);
 *
 * @param uri Data URI to POST to
 * @param auth_header Authorization header (format: "HeaderName:HeaderValue")
 * @param chunk_data Chunk data bytes
 * @param chunk_len Length of chunk data
 * @param user_data Must be a mds_uploader_t* instance
 *
 * @return 0 on success, negative error code on failure
 */
int mds_uploader_callback(const char *uri,
                          const char *auth_header,
                          const uint8_t *chunk_data,
                          size_t chunk_len,
                          void *user_data);

/**
 * @brief Get upload statistics
 *
 * Returns statistics about uploads performed by this uploader.
 *
 * @param uploader Uploader handle
 * @param stats Pointer to receive statistics
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_uploader_get_stats(mds_uploader_t *uploader,
                           mds_upload_stats_t *stats);

/**
 * @brief Reset upload statistics
 *
 * Resets the upload statistics counters to zero.
 *
 * @param uploader Uploader handle
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_uploader_reset_stats(mds_uploader_t *uploader);

/**
 * @brief Set HTTP timeout for uploads
 *
 * Sets the timeout for HTTP requests. Default is 30 seconds.
 *
 * @param uploader Uploader handle
 * @param timeout_ms Timeout in milliseconds
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_uploader_set_timeout(mds_uploader_t *uploader,
                             long timeout_ms);

/**
 * @brief Enable/disable verbose output
 *
 * When enabled, prints detailed information about HTTP requests.
 *
 * @param uploader Uploader handle
 * @param verbose true to enable verbose output, false to disable
 *
 * @return 0 on success, negative error code otherwise
 */
int mds_uploader_set_verbose(mds_uploader_t *uploader,
                             bool verbose);

#ifdef __cplusplus
}
#endif

#endif /* MEMFAULT_MDS_UPLOAD_H */
