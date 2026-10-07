/* SPDX-License-Identifier: GPL-3.0-or-later */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ch341_async.h"
#include <stddef.h>

#define CH341_OUT_ENDPOINT 0x02U
#define CH341_IN_ENDPOINT 0x82U
#define EVENT_SLICE_MS 50U

struct exchange_slot {
    struct libusb_transfer *transfer;
    int active;
    int completed;
    int result;
};

static uint64_t monotonic_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
#endif
}

static void pause_after_event_error(void)
{
    /* A failed event handler can return immediately. Avoid spinning during
     * recovery, but never pretend that its error returned transfer ownership. */
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec delay = {0, 1000000L};
    (void)nanosleep(&delay, NULL);
#endif
}

static int transfer_result(const struct libusb_transfer *transfer)
{
    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        return transfer->actual_length == transfer->length ? 0 : LIBUSB_ERROR_IO;
    case LIBUSB_TRANSFER_TIMED_OUT:
        return LIBUSB_ERROR_TIMEOUT;
    case LIBUSB_TRANSFER_CANCELLED:
        return LIBUSB_ERROR_INTERRUPTED;
    case LIBUSB_TRANSFER_STALL:
        return LIBUSB_ERROR_PIPE;
    case LIBUSB_TRANSFER_NO_DEVICE:
        return LIBUSB_ERROR_NO_DEVICE;
    case LIBUSB_TRANSFER_OVERFLOW:
        return LIBUSB_ERROR_OVERFLOW;
    default:
        return LIBUSB_ERROR_IO;
    }
}

static void LIBUSB_CALL exchange_complete(struct libusb_transfer *transfer)
{
    struct exchange_slot *slot = (struct exchange_slot *)transfer->user_data;
    slot->result = transfer_result(transfer);
    slot->completed = 1;
    slot->active = 0;
}

static unsigned int active_count(const struct exchange_slot *slots,
                                 unsigned int count)
{
    unsigned int active = 0;
    for (unsigned int i = 0; i < count; ++i)
        active += slots[i].active != 0;
    return active;
}

static int submit(const struct ch341_async_api *api, struct exchange_slot *slot,
                  libusb_device_handle *handle, unsigned char endpoint,
                  uint8_t *buffer, unsigned int length, unsigned int timeout)
{
    int ret;
    slot->completed = 0;
    slot->result = 0;
    libusb_fill_bulk_transfer(slot->transfer, handle, endpoint, buffer,
                             (int)length, exchange_complete, slot, timeout);
    /* Set before submission; ownership reverts immediately only if it fails. */
    slot->active = 1;
    ret = api->submit_transfer(slot->transfer);
    if (ret != 0) {
        slot->active = 0;
        return ret < 0 ? ret : LIBUSB_ERROR_IO;
    }
    return 0;
}

int ch341_async_exchange(const struct ch341_async_api *api,
                         libusb_context *context, libusb_device_handle *handle,
                         const uint8_t *tx, unsigned int tx_len,
                         uint8_t *rx, unsigned int rx_len,
                         unsigned int timeout_ms)
{
    struct exchange_slot slots[1U + CH341_ASYNC_IN_FLIGHT] = {{0}};
    unsigned int reads, count, next_rx = 0;
    uint64_t started, now;
    int ret = 0;

    if (!api || !api->alloc_transfer || !api->free_transfer ||
        !api->submit_transfer || !api->cancel_transfer ||
        !api->handle_events_timeout || !handle || !timeout_ms ||
        tx_len > CH341_ASYNC_MAX_BYTES || rx_len > CH341_ASYNC_MAX_BYTES ||
        (tx_len && !tx) || (rx_len && !rx))
        return LIBUSB_ERROR_INVALID_PARAM;
    if (!tx_len && !rx_len)
        return 0;

    reads = (rx_len + CH341_ASYNC_REPLY_BYTES - 1U) / CH341_ASYNC_REPLY_BYTES;
    if (reads > CH341_ASYNC_IN_FLIGHT)
        reads = CH341_ASYNC_IN_FLIGHT;
    count = 1U + reads;
    /* Allocate before starting any I/O. Slot zero is always the OUT slot. */
    for (unsigned int i = 0; i < count; ++i) {
        if (i == 0 && !tx_len)
            continue;
        slots[i].transfer = api->alloc_transfer(0);
        if (!slots[i].transfer) {
            ret = LIBUSB_ERROR_NO_MEM;
            goto cleanup;
        }
    }
    started = monotonic_ms();
    if (started == UINT64_MAX) {
        ret = LIBUSB_ERROR_OTHER;
        goto cleanup;
    }
    if (tx_len) {
        ret = submit(api, &slots[0], handle, CH341_OUT_ENDPOINT,
                     (uint8_t *)tx, tx_len, timeout_ms);
        if (ret != 0)
            goto cleanup;
    }

    for (;;) {
        unsigned int remaining;
        struct timeval wait;
        /* Check all completions before refilling any slot. Callback order is
         * unrelated to slot order; each buffer keeps its submission offset. */
        for (unsigned int i = 0; i < count; ++i) {
            if (slots[i].completed && slots[i].result != 0) {
                ret = slots[i].result;
                goto cleanup;
            }
        }
        if (next_rx == rx_len && active_count(slots, count) == 0)
            break;
        now = monotonic_ms();
        if (now == UINT64_MAX || now < started) {
            ret = LIBUSB_ERROR_OTHER;
            goto cleanup;
        }
        if (now - started >= timeout_ms) {
            ret = LIBUSB_ERROR_TIMEOUT;
            goto cleanup;
        }
        remaining = timeout_ms - (unsigned int)(now - started);
        for (unsigned int i = 1; i < count && next_rx < rx_len; ++i) {
            unsigned int bytes;
            if (slots[i].active)
                continue;
            bytes = rx_len - next_rx;
            if (bytes > CH341_ASYNC_REPLY_BYTES)
                bytes = CH341_ASYNC_REPLY_BYTES;
            ret = submit(api, &slots[i], handle, CH341_IN_ENDPOINT,
                         rx + next_rx, bytes, remaining);
            if (ret != 0)
                goto cleanup;
            next_rx += bytes;
        }
        /* IN is queued before waiting for OUT. Waiting for a large OUT first
         * can fill the CH341 reply FIFO and deadlock both directions. */
        if (remaining > EVENT_SLICE_MS)
            remaining = EVENT_SLICE_MS;
        wait.tv_sec = 0;
        wait.tv_usec = (long)remaining * 1000L;
        ret = api->handle_events_timeout(context, &wait);
        if (ret == LIBUSB_ERROR_INTERRUPTED) {
            ret = 0;
            continue;
        }
        if (ret != 0) {
            if (ret > 0)
                ret = LIBUSB_ERROR_IO;
            goto cleanup;
        }
    }

cleanup:
    if (active_count(slots, count)) {
        for (unsigned int i = 0; i < count; ++i) {
            if (slots[i].active)
                (void)api->cancel_transfer(slots[i].transfer);
        }
        /* A cancel return code never proves that the callback has run.
         * Retain every slot, user buffer and transfer until callbacks drain. */
        while (active_count(slots, count)) {
            struct timeval wait = {0, (long)EVENT_SLICE_MS * 1000L};
            int event_ret = api->handle_events_timeout(context, &wait);
            if (event_ret != 0 && active_count(slots, count))
                pause_after_event_error();
        }
    }
    for (unsigned int i = 0; i < count; ++i) {
        if (slots[i].transfer)
            api->free_transfer(slots[i].transfer);
    }
    return ret;
}
