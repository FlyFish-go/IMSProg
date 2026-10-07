/* Hardware-free tests of the actual CH341 asynchronous exchange helper. */
#define _POSIX_C_SOURCE 200809L
#include "windows/ch341_async.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TRANSFERS (1U + CH341_ASYNC_IN_FLIGHT)
#define TEST_CONTEXT ((libusb_context *)(uintptr_t)0x1234U)
#define TEST_HANDLE ((libusb_device_handle *)(uintptr_t)0x5678U)

struct record {
    struct libusb_transfer *transfer;
    unsigned int sequence;
    unsigned int offset;
    int active;
    int cancelled;
};

static struct {
    struct record records[MAX_TRANSFERS];
    uint8_t tx[CH341_ASYNC_MAX_BYTES + 2U];
    uint8_t rx[CH341_ASYNC_MAX_BYTES + 2U];
    unsigned int tx_len, rx_len;
    unsigned int allocations, allocation_calls, frees, submissions;
    unsigned int active, in_active, peak_in_active, out_submissions;
    unsigned int rx_scheduled, rx_completed, events, cancels;
    unsigned int allocation_fail_at, submit_fail_at, bad_sequence;
    enum libusb_transfer_status bad_status;
    int bad_length; /* -2: exact; -1: negative; 0: zero; 1: short; 2: excess. */
    unsigned int event_error_at, event_errors, interrupt_events, drain_delays;
    int event_error, cancel_error, order, batch, out_last, stall;
    unsigned int random;
} fake;

static unsigned int cases;

static uint8_t incoming_byte(unsigned int offset)
{
    return (uint8_t)((offset * 37U + offset / 251U) ^ 0xB6U);
}

static uint8_t outgoing_byte(unsigned int offset)
{
    return (uint8_t)(offset * 19U + offset / 257U);
}

static void reset_fake(unsigned int tx_len, unsigned int rx_len)
{
    memset(&fake, 0, sizeof(fake));
    fake.tx_len = tx_len;
    fake.rx_len = rx_len;
    fake.bad_status = LIBUSB_TRANSFER_COMPLETED;
    fake.bad_length = -2;
    fake.random = 73513U;
    fake.event_error = LIBUSB_ERROR_IO;
    memset(fake.rx, 0xA5, sizeof(fake.rx));
    memset(fake.tx, 0x5A, sizeof(fake.tx));
    for (unsigned int i = 0; i < tx_len; ++i)
        fake.tx[i + 1] = outgoing_byte(i);
}

static struct record *lookup(struct libusb_transfer *transfer)
{
    for (unsigned int i = 0; i < fake.allocations; ++i) {
        if (fake.records[i].transfer == transfer)
            return &fake.records[i];
    }
    assert(!"unknown or already freed transfer");
    return NULL;
}

static struct libusb_transfer *LIBUSB_CALL fake_alloc(int iso_packets)
{
    struct libusb_transfer *transfer;
    assert(iso_packets == 0);
    if (++fake.allocation_calls == fake.allocation_fail_at)
        return NULL;
    assert(fake.allocations < MAX_TRANSFERS);
    transfer = calloc(1, sizeof(*transfer));
    assert(transfer != NULL);
    fake.records[fake.allocations++].transfer = transfer;
    return transfer;
}

static void LIBUSB_CALL fake_free(struct libusb_transfer *transfer)
{
    struct record *record = lookup(transfer);
    assert(!record->active); /* Catch freeing before a cancelled callback. */
    assert(fake.active == 0); /* Cleanup must drain before freeing any slot. */
    record->transfer = NULL;
    ++fake.frees;
    free(transfer);
}

static int LIBUSB_CALL fake_submit(struct libusb_transfer *transfer)
{
    struct record *record = lookup(transfer);
    assert(!record->active);
    assert(transfer->dev_handle == TEST_HANDLE);
    assert(transfer->type == LIBUSB_TRANSFER_TYPE_BULK);
    assert(transfer->timeout > 0);
    assert(transfer->flags == 0);
    assert(transfer->callback && transfer->user_data);
    assert(transfer->length > 0);
    if (++fake.submissions == fake.submit_fail_at)
        return LIBUSB_ERROR_BUSY;
    record->sequence = fake.submissions;
    record->active = 1;
    record->cancelled = 0;
    ++fake.active;
    assert(fake.active <= MAX_TRANSFERS);
    if (transfer->endpoint == 0x02) {
        assert(++fake.out_submissions == 1);
        assert(transfer->buffer == fake.tx + 1);
        assert((unsigned int)transfer->length == fake.tx_len);
    } else {
        assert(transfer->endpoint == 0x82);
        assert((unsigned int)transfer->length <= CH341_ASYNC_REPLY_BYTES);
        assert(transfer->buffer == fake.rx + 1 + fake.rx_scheduled);
        record->offset = fake.rx_scheduled;
        fake.rx_scheduled += (unsigned int)transfer->length;
        assert(fake.rx_scheduled <= fake.rx_len);
        ++fake.in_active;
        assert(fake.in_active <= CH341_ASYNC_IN_FLIGHT);
        if (fake.in_active > fake.peak_in_active)
            fake.peak_in_active = fake.in_active;
    }
    return 0;
}

static int LIBUSB_CALL fake_cancel(struct libusb_transfer *transfer)
{
    struct record *record = lookup(transfer);
    assert(record->active);
    ++fake.cancels;
    /* Failed cancellations leave I/O active; a subsequent event delivers a
     * naturally completed transfer instead. NOT_FOUND models the callback race. */
    if (fake.cancel_error)
        return fake.cancel_error;
    record->cancelled = 1;
    return 0;
}

static void complete(struct record *record)
{
    struct libusb_transfer *transfer = record->transfer;
    int length = transfer->length;
    assert(record->active);
    record->active = 0;
    --fake.active;
    transfer->status = record->cancelled ? LIBUSB_TRANSFER_CANCELLED
                                         : LIBUSB_TRANSFER_COMPLETED;
    if (!record->cancelled && record->sequence == fake.bad_sequence) {
        transfer->status = fake.bad_status;
        switch (fake.bad_length) {
        case -1: length = -1; break;
        case 0: length = 0; break;
        case 1: --length; break;
        case 2: ++length; break;
        default: break;
        }
    }
    transfer->actual_length = length;
    if (transfer->endpoint == 0x82) {
        --fake.in_active;
        if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
            int to_copy = length < transfer->length ? length : transfer->length;
            for (int i = 0; i < to_copy; ++i)
                transfer->buffer[i] = incoming_byte(record->offset + (unsigned int)i);
            fake.rx_completed += (unsigned int)transfer->length;
        }
    }
    transfer->callback(transfer);
}

static struct record *choose_record(void)
{
    struct record *selected = NULL;
    unsigned int selected_rank = 0;
    for (unsigned int i = 0; i < fake.allocations; ++i) {
        struct record *record = &fake.records[i];
        unsigned int rank;
        if (!record->active)
            continue;
        if (fake.out_last && !fake.cancels && record->transfer->endpoint == 0x02 &&
            fake.rx_completed < fake.rx_len)
            continue;
        switch (fake.order) {
        case 0: rank = 100000U - record->sequence; break; /* FIFO */
        case 1: rank = record->sequence; break; /* Reverse callbacks */
        default:
            fake.random = fake.random * 1664525U + 1013904223U;
            rank = fake.random;
            break;
        }
        if (!selected || rank > selected_rank) {
            selected = record;
            selected_rank = rank;
        }
    }
    return selected;
}

static int LIBUSB_CALL fake_events(libusb_context *context, struct timeval *wait)
{
    struct record *record;
    assert(context == TEST_CONTEXT);
    assert(wait->tv_sec == 0 && wait->tv_usec > 0 && wait->tv_usec <= 50000);
    assert(++fake.events < 10000); /* A regression must fail rather than hang. */
    if (fake.events == 1 && !fake.submit_fail_at && !fake.cancels) {
        unsigned int expected = (fake.rx_len + 30U) / 31U;
        if (expected > CH341_ASYNC_IN_FLIGHT)
            expected = CH341_ASYNC_IN_FLIGHT;
        assert(fake.in_active == expected); /* Reads precede any event wait. */
    }
    if (fake.interrupt_events) {
        --fake.interrupt_events;
        return LIBUSB_ERROR_INTERRUPTED;
    }
    if (fake.event_errors && fake.events >= fake.event_error_at) {
        --fake.event_errors;
        return fake.event_error;
    }
    if (fake.cancels && fake.drain_delays) {
        --fake.drain_delays;
        return 0;
    }
    if (fake.stall && !fake.cancels) {
        struct timespec delay = {0, 1000000L};
        (void)nanosleep(&delay, NULL);
        return 0;
    }
    do {
        record = choose_record();
        if (!record) {
            /* The OUT is deliberately back-pressured until later IN batches;
             * return so the helper can refill its newly available slots. */
            assert(fake.batch && fake.out_last && !fake.in_active &&
                   fake.rx_scheduled < fake.rx_len);
            break;
        }
        complete(record);
    } while (fake.batch && fake.active);
    return 0;
}

static const struct ch341_async_api api = {
    fake_alloc, fake_free, fake_submit, fake_cancel, fake_events
};

static void check_result(int actual, int expected)
{
    if (actual != expected)
        fprintf(stderr, "case %u: got %d, expected %d\n", cases, actual, expected);
    assert(actual == expected);
    assert(fake.active == 0 && fake.in_active == 0);
    assert(fake.allocations == fake.frees);
    assert(fake.rx[0] == 0xA5);
    for (unsigned int i = fake.rx_len + 1; i < sizeof(fake.rx); ++i)
        assert(fake.rx[i] == 0xA5);
    assert(fake.tx[0] == 0x5A);
    for (unsigned int i = 0; i < fake.tx_len; ++i)
        assert(fake.tx[i + 1] == outgoing_byte(i));
    for (unsigned int i = fake.tx_len + 1; i < sizeof(fake.tx); ++i)
        assert(fake.tx[i] == 0x5A);
    if (expected == 0) {
        assert(fake.rx_scheduled == fake.rx_len);
        assert(fake.rx_completed == fake.rx_len);
        assert(fake.out_submissions == (fake.tx_len != 0));
        for (unsigned int i = 0; i < fake.rx_len; ++i)
            assert(fake.rx[i + 1] == incoming_byte(i));
        assert(fake.cancels == 0);
        if (fake.rx_len >= 32U * 31U)
            assert(fake.peak_in_active == 32);
    }
    ++cases;
}

static void run_case(int expected)
{
    check_result(ch341_async_exchange(&api, TEST_CONTEXT, TEST_HANDLE,
                                      fake.tx + 1, fake.tx_len,
                                      fake.rx + 1, fake.rx_len,
                                      fake.stall ? 20U : 3000U), expected);
}

static void test_success(void)
{
    static const unsigned int lengths[] = {
        0, 1, 30, 31, 32, 62, 63, 991, 992, 993, 2048, 2112, 4096, 8192
    };
    for (unsigned int i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        for (int order = 0; order < 3; ++order) {
            for (int batch = 0; batch < 2; ++batch) {
                for (int read_only = 0; read_only < 2; ++read_only) {
                    reset_fake(read_only ? 0 : 8192, lengths[i]);
                    fake.order = order;
                    fake.batch = batch;
                    fake.out_last = 1;
                    run_case(0);
                }
            }
        }
    }
    reset_fake(8192, 8192);
    fake.order = 0; /* OUT completes first. */
    run_case(0);
    reset_fake(1, 1);
    fake.interrupt_events = 10;
    run_case(0);
}

static void test_allocations_and_submissions(void)
{
    for (unsigned int fail = 1; fail <= MAX_TRANSFERS; ++fail) {
        reset_fake(8192, 8192);
        fake.allocation_fail_at = fail;
        run_case(LIBUSB_ERROR_NO_MEM);
        assert(fake.submissions == 0 && fake.events == 0);
    }
    for (unsigned int fail = 1; fail <= 1U + (8192U + 30U) / 31U; ++fail) {
        reset_fake(8192, 8192);
        fake.submit_fail_at = fail;
        fake.order = 2;
        fake.out_last = 1;
        fake.drain_delays = 3;
        run_case(LIBUSB_ERROR_BUSY);
        assert(fake.submissions == fail);
    }
}

static void test_bad_completions(void)
{
    static const unsigned int sequences[] = {1, 2, 18, 33, 34, 135, 266};
    static const struct {
        enum libusb_transfer_status status;
        int error;
    } statuses[] = {
        {LIBUSB_TRANSFER_ERROR, LIBUSB_ERROR_IO},
        {LIBUSB_TRANSFER_TIMED_OUT, LIBUSB_ERROR_TIMEOUT},
        {LIBUSB_TRANSFER_CANCELLED, LIBUSB_ERROR_INTERRUPTED},
        {LIBUSB_TRANSFER_STALL, LIBUSB_ERROR_PIPE},
        {LIBUSB_TRANSFER_NO_DEVICE, LIBUSB_ERROR_NO_DEVICE},
        {LIBUSB_TRANSFER_OVERFLOW, LIBUSB_ERROR_OVERFLOW}
    };
    for (unsigned int i = 0; i < sizeof(sequences) / sizeof(sequences[0]); ++i) {
        for (int length = -1; length <= 2; ++length) {
            reset_fake(8192, 8192);
            fake.bad_sequence = sequences[i];
            fake.bad_length = length;
            fake.order = 2;
            fake.out_last = 1;
            run_case(LIBUSB_ERROR_IO);
        }
        for (unsigned int j = 0; j < sizeof(statuses) / sizeof(statuses[0]); ++j) {
            reset_fake(8192, 8192);
            fake.bad_sequence = sequences[i];
            fake.bad_status = statuses[j].status;
            fake.order = 1;
            fake.out_last = 1;
            run_case(statuses[j].error);
        }
    }
    /* A one-byte zero completion must not be mistaken for an idle slot. */
    reset_fake(1, 1);
    fake.bad_sequence = 2;
    fake.bad_length = 0;
    run_case(LIBUSB_ERROR_IO);
    /* Error in the last outstanding transfer, without another callback to
     * keep the event loop alive. Also exercise both one-direction exchanges. */
    reset_fake(1, 0);
    fake.bad_sequence = 1;
    fake.bad_length = 0;
    run_case(LIBUSB_ERROR_IO);
    reset_fake(0, 1);
    fake.bad_sequence = 1;
    fake.bad_status = LIBUSB_TRANSFER_TIMED_OUT;
    run_case(LIBUSB_ERROR_TIMEOUT);
}

static void test_cancellation_and_events(void)
{
    static const int cancel_errors[] = {
        0, LIBUSB_ERROR_NOT_FOUND, LIBUSB_ERROR_IO, LIBUSB_ERROR_NO_DEVICE
    };
    for (unsigned int i = 0; i < sizeof(cancel_errors) / sizeof(cancel_errors[0]); ++i) {
        reset_fake(8192, 8192);
        fake.event_error_at = 2;
        fake.event_errors = 4; /* The drain also survives transient event errors. */
        fake.cancel_error = cancel_errors[i];
        fake.drain_delays = 10;
        fake.order = 2;
        run_case(LIBUSB_ERROR_IO);
        assert(fake.cancels > 0);
        assert(fake.event_errors == 0 && fake.drain_delays == 0);
    }
    reset_fake(8192, 8192);
    fake.event_error_at = 1;
    fake.event_errors = 1;
    fake.event_error = LIBUSB_ERROR_NO_DEVICE;
    run_case(LIBUSB_ERROR_NO_DEVICE);
    reset_fake(8192, 8192);
    fake.stall = 1; /* Whole-exchange watchdog, with no driver completions. */
    run_case(LIBUSB_ERROR_TIMEOUT);
    assert(fake.cancels > 0);
}

static void test_validation(void)
{
    struct ch341_async_api invalid;
    reset_fake(0, 0);
    assert(ch341_async_exchange(NULL, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    assert(ch341_async_exchange(&api, TEST_CONTEXT, NULL, NULL, 0, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    assert(ch341_async_exchange(&api, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 0, 0)
           == LIBUSB_ERROR_INVALID_PARAM);
    assert(ch341_async_exchange(&api, TEST_CONTEXT, TEST_HANDLE, NULL, 1, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    assert(ch341_async_exchange(&api, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 1, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    assert(ch341_async_exchange(&api, TEST_CONTEXT, TEST_HANDLE,
                                fake.tx, 8193, NULL, 0, 1) == LIBUSB_ERROR_INVALID_PARAM);
    assert(ch341_async_exchange(&api, TEST_CONTEXT, TEST_HANDLE,
                                NULL, 0, fake.rx, 8193, 1) == LIBUSB_ERROR_INVALID_PARAM);
    invalid = api; invalid.alloc_transfer = NULL;
    assert(ch341_async_exchange(&invalid, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    invalid = api; invalid.free_transfer = NULL;
    assert(ch341_async_exchange(&invalid, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    invalid = api; invalid.submit_transfer = NULL;
    assert(ch341_async_exchange(&invalid, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    invalid = api; invalid.cancel_transfer = NULL;
    assert(ch341_async_exchange(&invalid, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    invalid = api; invalid.handle_events_timeout = NULL;
    assert(ch341_async_exchange(&invalid, TEST_CONTEXT, TEST_HANDLE, NULL, 0, NULL, 0, 1)
           == LIBUSB_ERROR_INVALID_PARAM);
    assert(fake.allocation_calls == 0 && fake.submissions == 0);
    cases += 12;
}

int main(void)
{
    test_success();
    test_allocations_and_submissions();
    test_bad_completions();
    test_cancellation_and_events();
    test_validation();
    printf("CH341 async: %u hardware-free cases passed; exact bytes, 32-read window, "
           "reordered callbacks, short/error/timeout handling and complete cancellation drain\n", cases);
    return 0;
}
