/**
 * @file mds_upload.c
 * @brief HTTP uploader implementation using libcurl
 */

#include "memfault_hid/mds_upload.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>

/* Uploader structure */
struct mds_uploader {
    CURL *curl;
    struct curl_slist *headers;
    mds_upload_stats_t stats;
    long timeout_ms;
    bool verbose;
};

/* ============================================================================
 * Uploader Management
 * ========================================================================== */

mds_uploader_t *mds_uploader_create(void) {
    mds_uploader_t *uploader = calloc(1, sizeof(mds_uploader_t));
    if (uploader == NULL) {
        return NULL;
    }

    /* Initialize libcurl */
    uploader->curl = curl_easy_init();
    if (uploader->curl == NULL) {
        free(uploader);
        return NULL;
    }

    /* Set default timeout (30 seconds) */
    uploader->timeout_ms = 30000;
    uploader->verbose = false;

    return uploader;
}

void mds_uploader_destroy(mds_uploader_t *uploader) {
    if (uploader == NULL) {
        return;
    }

    if (uploader->headers) {
        curl_slist_free_all(uploader->headers);
    }

    if (uploader->curl) {
        curl_easy_cleanup(uploader->curl);
    }

    free(uploader);
}

/* ============================================================================
 * Upload Callback
 * ========================================================================== */

int mds_uploader_callback(const char *uri,
                          const char *auth_header,
                          const uint8_t *chunk_data,
                          size_t chunk_len,
                          void *user_data) {
    if (uri == NULL || auth_header == NULL || chunk_data == NULL || user_data == NULL) {
        return -EINVAL;
    }

    mds_uploader_t *uploader = (mds_uploader_t *)user_data;
    CURLcode res;

    /* Reset curl for new request */
    curl_easy_reset(uploader->curl);

    /* Set URL */
    curl_easy_setopt(uploader->curl, CURLOPT_URL, uri);

    /* Set POST method */
    curl_easy_setopt(uploader->curl, CURLOPT_POST, 1L);

    /* Set POST data */
    curl_easy_setopt(uploader->curl, CURLOPT_POSTFIELDS, chunk_data);
    curl_easy_setopt(uploader->curl, CURLOPT_POSTFIELDSIZE, (long)chunk_len);

    /* Parse authorization header (format: "HeaderName:HeaderValue") */
    const char *colon = strchr(auth_header, ':');
    if (colon == NULL) {
        fprintf(stderr, "Invalid authorization header format: %s\n", auth_header);
        uploader->stats.upload_failures++;
        return -EINVAL;
    }

    /* Extract header name and value */
    size_t header_name_len = colon - auth_header;
    char *header_name = malloc(header_name_len + 1);
    if (header_name == NULL) {
        uploader->stats.upload_failures++;
        return -ENOMEM;
    }
    memcpy(header_name, auth_header, header_name_len);
    header_name[header_name_len] = '\0';

    const char *header_value = colon + 1;

    /* Build full header string for curl */
    size_t full_header_len = strlen(header_name) + 2 + strlen(header_value) + 1; /* name + ": " + value + \0 */
    char *full_header = malloc(full_header_len);
    if (full_header == NULL) {
        free(header_name);
        uploader->stats.upload_failures++;
        return -ENOMEM;
    }
    snprintf(full_header, full_header_len, "%s: %s", header_name, header_value);

    /* Set headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, full_header);
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");

    curl_easy_setopt(uploader->curl, CURLOPT_HTTPHEADER, headers);

    /* Set timeout */
    curl_easy_setopt(uploader->curl, CURLOPT_TIMEOUT_MS, uploader->timeout_ms);

    /* Set verbose if enabled */
    if (uploader->verbose) {
        curl_easy_setopt(uploader->curl, CURLOPT_VERBOSE, 1L);
    }

    /* Perform the request */
    res = curl_easy_perform(uploader->curl);

    /* Get HTTP status code */
    long http_code = 0;
    curl_easy_getinfo(uploader->curl, CURLINFO_RESPONSE_CODE, &http_code);
    uploader->stats.last_http_status = http_code;

    /* Clean up headers */
    curl_slist_free_all(headers);
    free(full_header);
    free(header_name);

    /* Check result */
    if (res != CURLE_OK) {
        fprintf(stderr, "Upload failed: %s\n", curl_easy_strerror(res));
        uploader->stats.upload_failures++;
        return -EIO;
    }

    /* Check HTTP status */
    if (http_code < 200 || http_code >= 300) {
        fprintf(stderr, "Upload failed with HTTP status %ld\n", http_code);
        uploader->stats.upload_failures++;
        return -EIO;
    }

    /* Success - update stats */
    uploader->stats.chunks_uploaded++;
    uploader->stats.bytes_uploaded += chunk_len;

    if (uploader->verbose) {
        printf("Uploaded chunk: %zu bytes, HTTP %ld\n", chunk_len, http_code);
    }

    return 0;
}

/* ============================================================================
 * Statistics
 * ========================================================================== */

int mds_uploader_get_stats(mds_uploader_t *uploader,
                           mds_upload_stats_t *stats) {
    if (uploader == NULL || stats == NULL) {
        return -EINVAL;
    }

    *stats = uploader->stats;
    return 0;
}

int mds_uploader_reset_stats(mds_uploader_t *uploader) {
    if (uploader == NULL) {
        return -EINVAL;
    }

    memset(&uploader->stats, 0, sizeof(uploader->stats));
    return 0;
}

/* ============================================================================
 * Configuration
 * ========================================================================== */

int mds_uploader_set_timeout(mds_uploader_t *uploader,
                             long timeout_ms) {
    if (uploader == NULL) {
        return -EINVAL;
    }

    uploader->timeout_ms = timeout_ms;
    return 0;
}

int mds_uploader_set_verbose(mds_uploader_t *uploader,
                             bool verbose) {
    if (uploader == NULL) {
        return -EINVAL;
    }

    uploader->verbose = verbose;
    return 0;
}
