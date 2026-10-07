/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IMSPROG_CH341_ASYNC_H
#define IMSPROG_CH341_ASYNC_H

#include "vendor/libusb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH341_ASYNC_MAX_BYTES 8192U
#define CH341_ASYNC_IN_FLIGHT 32U
#define CH341_ASYNC_REPLY_BYTES 31U

/* All functions are resolved from the same, still-loaded libusb DLL. */
struct ch341_async_api {
    struct libusb_transfer *(LIBUSB_CALL *alloc_transfer)(int);
    void (LIBUSB_CALL *free_transfer)(struct libusb_transfer *);
    int (LIBUSB_CALL *submit_transfer)(struct libusb_transfer *);
    int (LIBUSB_CALL *cancel_transfer)(struct libusb_transfer *);
    int (LIBUSB_CALL *handle_events_timeout)(libusb_context *, struct timeval *);
};

/*
 * One OUT batch on 0x02, concurrent with up to 32 packet-sized IN reads on
 * 0x82. Both lengths must be <= 8192 and timeout_ms must be nonzero. The
 * timeout covers the whole exchange, not a fresh budget for each IN batch.
 * Zero means every requested byte completed exactly; a negative libusb error
 * includes short/zero completions. On error, rx may contain partial results.
 *
 * The caller must exclusively own event handling for this context during the
 * call, serialize exchanges, and keep buffers, handle, context and DLL alive.
 * Every successfully submitted transfer is drained before this returns.
 * Cancellation is asynchronous: even NOT_FOUND or another cancellation error
 * does not release transfer ownership. If a broken libusb/driver never delivers
 * completion callbacks, error cleanup cannot have a finite deadline without
 * risking use-after-free. In that exceptional case this call remains in the
 * drain loop rather than return with live transfers or free their storage.
 */
int ch341_async_exchange(const struct ch341_async_api *api,
                         libusb_context *context, libusb_device_handle *handle,
                         const uint8_t *tx, unsigned int tx_len,
                         uint8_t *rx, unsigned int rx_len,
                         unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif
#endif
