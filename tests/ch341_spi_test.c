/* Hardware-free tests of production Windows CH341A/B SPI framing. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ch341a_spi.h"
#include "windows/ch347_win32.h"

#define BATCH_DATA (127u * 31u)
static unsigned int opens, closes, exchanges, pin_commands, spi_batches;
static unsigned int config_commands, cs_transitions, last_spi_tx, last_spi_rx;
static int open_result, fail_exchange_result;
static unsigned int fail_exchange_at;
static bool pins_enabled, cs_low;
static uint8_t last_config;
static const uint8_t *expected_write;
static unsigned int expected_write_count, expected_read_count, data_seen;
static struct ch347_priv *active_context;

/* Deliberately independent, simple reference implementation. */
static uint8_t reversed(uint8_t value)
{
    uint8_t result = 0;
    for (unsigned int i = 0; i < 8; ++i) {
        result = (uint8_t)((result << 1) | (value & 1u));
        value >>= 1;
    }
    return result;
}

static uint8_t reply_value(unsigned int index)
{
    return (uint8_t)((index * 73u + (index >> 3) + 0x16u) & 0xffu);
}

int ch341_windows_open(struct ch347_priv *p)
{
    ++opens;
    assert(!active_context && !p->windows_backend);
    /* Model a partially allocated open, including the failed-open case. */
    active_context = p;
    p->windows_backend = p;
    return open_result;
}

void ch347_windows_close(struct ch347_priv *p)
{
    ++closes;
    assert(p == active_context && p->windows_backend == p);
    active_context = NULL;
    p->windows_backend = NULL;
    pins_enabled = cs_low = false;
}

int ch341_windows_exchange(struct ch347_priv *p, const uint8_t *tx,
                           unsigned int tx_len, uint8_t *rx, unsigned int rx_len)
{
    unsigned int pos, count, start = data_seen;
    assert(p == active_context && p->windows_backend == p);
    assert(tx && tx_len && tx_len <= 4096);
    assert(rx || !rx_len);
    ++exchanges;
    if (tx[0] == 0xaa) {
        assert(tx_len == 3 && !rx_len && tx[2] == 0);
        assert((tx[1] & 0xf8u) == 0x60);
        ++config_commands;
        last_config = tx[1];
    } else if (tx[0] == 0xab) {
        bool enabling;
        assert(tx_len == 9 && !rx_len && tx[8] == 0x20);
        assert(tx[7] == 0x7f || tx[7] == 0x40);
        enabling = tx[7] == 0x7f;
        for (unsigned int i = 1; i < 6; ++i)
            assert(tx[i] == 0xb7);
        assert(tx[6] == (enabling ? 0xb6 : 0xb7));
        if (cs_low)
            ++cs_transitions;
        cs_low = false; /* CS high is driven before DIR changes. */
        if (enabling) {
            ++cs_transitions;
            cs_low = true;
        }
        pins_enabled = enabling;
        ++pin_commands;
    } else {
        assert(tx_len > 32 && rx_len > 0 && rx_len <= BATCH_DATA);
        for (unsigned int i = 0; i < 32; ++i)
            assert(tx[i] == 0);
        count = min(BATCH_DATA, expected_write_count + expected_read_count - start);
        assert(rx_len == count);
        assert(tx_len == 32u + count + (count + 30u) / 31u);
        pos = 32;
        for (unsigned int i = 0; i < count; ++i) {
            if (i % 31u == 0)
                assert(tx[pos++] == 0xa8);
            assert(tx[pos++] == (start + i < expected_write_count
                ? reversed(expected_write[start + i]) : 0xff));
            rx[i] = reversed(reply_value(start + i));
        }
        assert(pos == tx_len);
        data_seen += count;
        last_spi_tx = tx_len;
        last_spi_rx = rx_len;
        ++spi_batches;
    }
    if (exchanges == fail_exchange_at)
        return fail_exchange_result;
    return 0;
}

static void expect_spi(const uint8_t *write, unsigned int writes, unsigned int reads)
{
    expected_write = write;
    expected_write_count = writes;
    expected_read_count = reads;
    data_seen = 0;
}

static void clear_counters(void)
{
    assert(!active_context);
    opens = closes = exchanges = pin_commands = spi_batches = 0;
    config_commands = cs_transitions = last_spi_tx = last_spi_rx = 0;
    fail_exchange_at = 0;
    fail_exchange_result = open_result = 0;
    last_config = 0;
    expect_spi(NULL, 0, 0);
}

static void test_lifecycle_and_protocols(void)
{
    const uint8_t allowed[] = {0, 3, 4, 5, 6};
    uint8_t descriptor[20];
    clear_counters();
    assert(enable_pins(true) == -ENODEV);
    assert(config_stream(3) == -ENODEV);
    assert(ch341a_spi_send_command(0, 0, NULL, NULL) == -ENODEV);
    assert(ch341a_spi_shutdown() == -ENODEV);
    for (unsigned int chip = 0; chip <= 255; ++chip) {
        if (chip == 0 || (chip >= 3 && chip <= 6))
            continue;
        assert(ch341a_init((uint8_t)chip, 400) == -ENOTSUP);
    }
    assert(ch341a_init_i2c() == -ENOTSUP);
    assert(opens == 0 && exchanges == 0);
    memset(descriptor, 0xa5, sizeof(descriptor));
    assert(ch341GetDescriptor(descriptor + 1) == -ENOTSUP);
    assert(descriptor[0] == 0xa5 && descriptor[19] == 0xa5);
    for (unsigned int i = 1; i <= 18; ++i)
        assert(descriptor[i] == 0);
    assert(ch341GetDescriptor(NULL) == -EINVAL);
    for (unsigned int i = 0; i < sizeof(allowed); ++i) {
        assert(ch341a_init(allowed[i], (uint16_t)(i * 1234)) == 0);
        assert(config_commands == i + 1 && last_config == 0x63);
        assert(pins_enabled && cs_low);
        assert(ch341a_spi_init() == -EBUSY);
        assert(opens == i + 1);
        assert(ch341a_spi_shutdown() == 0);
        assert(!active_context && !pins_enabled && !cs_low);
        assert(closes == i + 1);
    }
}

static void test_packet_boundaries(void)
{
    static uint8_t input[25000], output[25002];
    const unsigned int lengths[] = {0, 1, 2, 30, 31, 32, 61, 62, 63,
                                   256, BATCH_DATA - 1, BATCH_DATA,
                                   BATCH_DATA + 1, 2 * BATCH_DATA + 17};
    clear_counters();
    assert(ch341a_spi_init() == 0);
    for (unsigned int i = 0; i < sizeof(input); ++i)
        input[i] = (uint8_t)((i * 53u) ^ (i >> 5));
    /* Each write/read boundary can split either a packet or an OUT batch. */
    for (unsigned int w = 0; w < sizeof(lengths) / sizeof(lengths[0]); ++w) {
        for (unsigned int r = 0; r < sizeof(lengths) / sizeof(lengths[0]); ++r) {
            unsigned int old_batches = spi_batches;
            const unsigned int writes = lengths[w], reads = lengths[r];
            const unsigned int total = writes + reads;
            const unsigned int old_pins = pin_commands, old_edges = cs_transitions;
            memset(output, 0xa5, sizeof(output));
            expect_spi(writes ? input : NULL, writes, reads);
            assert(ch341a_spi_send_command(writes, reads, writes ? input : NULL,
                                           reads ? output + 1 : NULL) == 0);
            assert(data_seen == total);
            assert(spi_batches - old_batches == (total + BATCH_DATA - 1) / BATCH_DATA);
            assert(pin_commands == old_pins && cs_transitions == old_edges);
            assert(cs_low && pins_enabled);
            assert(output[0] == 0xa5 && output[reads + 1] == 0xa5);
            for (unsigned int i = 0; i < reads; ++i)
                assert(output[i + 1] == reply_value(writes + i));
        }
    }
    /* The complete batch is exactly 4096 OUT bytes and 3937 echo bytes. */
    expect_spi(input, BATCH_DATA, 0);
    assert(ch341a_spi_send_command(BATCH_DATA, 0, input, NULL) == 0);
    assert(last_spi_tx == 4096 && last_spi_rx == BATCH_DATA);
    assert(enable_pins(false) == 0);
    assert(!cs_low && !pins_enabled);
    assert(ch341a_spi_shutdown() == 0);
}

static void test_separate_command_and_data(void)
{
    uint8_t command[] = {0x0b, 0x12, 0x34, 0x56, 0x00};
    uint8_t result[100];
    unsigned int old_edges, old_pins;
    clear_counters();
    assert(ch341a_spi_init() == 0);
    assert(enable_pins(false) == 0);
    assert(enable_pins(true) == 0);
    old_edges = cs_transitions;
    old_pins = pin_commands;
    expect_spi(command, sizeof(command), 0);
    assert(ch341a_spi_send_command(sizeof(command), 0, command, NULL) == 0);
    expect_spi(NULL, 0, sizeof(result));
    assert(ch341a_spi_send_command(0, sizeof(result), NULL, result) == 0);
    assert(cs_low && pins_enabled && cs_transitions == old_edges && pin_commands == old_pins);
    for (unsigned int i = 0; i < sizeof(result); ++i)
        assert(result[i] == reply_value(i));
    assert(enable_pins(false) == 0);
    assert(cs_transitions == old_edges + 1 && !cs_low);
    assert(ch341a_spi_shutdown() == 0);
}

static void test_arguments_and_error_propagation(void)
{
    static uint8_t data[BATCH_DATA + 20];
    const int failures[] = {-EIO, -ETIMEDOUT, -ENODEV, 7};
    unsigned int before;
    clear_counters();
    assert(ch341a_spi_init() == 0);
    before = exchanges;
    assert(ch341a_spi_send_command(1, 0, NULL, NULL) == -EINVAL);
    assert(ch341a_spi_send_command(0, 1, NULL, NULL) == -EINVAL);
    assert(ch341a_spi_send_command(UINT_MAX, 1, data, data) == -EOVERFLOW);
    assert(ch341a_spi_send_command(1, UINT_MAX, data, data) == -EOVERFLOW);
    assert(ch341a_spi_send_command(0, 0, NULL, NULL) == 0);
    assert(exchanges == before);
    for (unsigned int i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        unsigned int old_pins = pin_commands, old_edges = cs_transitions;
        memset(data, 0xa5, sizeof(data));
        expect_spi(NULL, 0, sizeof(data));
        fail_exchange_at = exchanges + 1;
        fail_exchange_result = failures[i];
        assert(ch341a_spi_send_command(0, sizeof(data), NULL, data) ==
               (failures[i] > 0 ? -EIO : failures[i]));
        assert(exchanges == fail_exchange_at); /* No retry or second batch. */
        assert(pin_commands == old_pins && cs_transitions == old_edges);
        for (unsigned int j = 0; j < sizeof(data); ++j)
            assert(data[j] == 0xa5); /* Failed replies are never copied. */
    }
    expect_spi(NULL, 0, sizeof(data));
    fail_exchange_at = exchanges + 2;
    fail_exchange_result = -EIO; /* A short second batch fails the whole call. */
    assert(ch341a_spi_send_command(0, sizeof(data), NULL, data) == -EIO);
    assert(exchanges == fail_exchange_at);
    for (unsigned int i = 0; i < sizeof(data); ++i)
        assert(data[i] == (i < BATCH_DATA ? reply_value(i) : 0xa5));
    fail_exchange_at = exchanges + 1;
    fail_exchange_result = -ETIMEDOUT;
    assert(ch341a_spi_shutdown() == -ETIMEDOUT);
    assert(closes == 1 && !active_context);
    assert(ch341a_spi_send_command(0, 1, NULL, data) == -ENODEV);
}

static void test_initialization_cleanup(void)
{
    const int failures[] = {-EIO, -ETIMEDOUT, 1};
    for (unsigned int i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        clear_counters();
        open_result = failures[i];
        assert(ch341a_spi_init() == (failures[i] > 0 ? -EIO : failures[i]));
        assert(opens == 1 && closes == 1 && exchanges == 0 && !active_context);
        open_result = 0;
        assert(ch341a_spi_init() == 0);
        assert(ch341a_spi_shutdown() == 0);
        for (unsigned int phase = 1; phase <= 2; ++phase) {
            clear_counters();
            fail_exchange_at = phase;
            fail_exchange_result = failures[i];
            assert(ch341a_spi_init() == (failures[i] > 0 ? -EIO : failures[i]));
            assert(opens == 1 && closes == 1 && exchanges == phase + 1);
            assert(!active_context && !pins_enabled && !cs_low);
            assert(ch341a_spi_shutdown() == -ENODEV);
            fail_exchange_at = 0;
            assert(ch341a_spi_init() == 0);
            assert(ch341a_spi_shutdown() == 0);
        }
    }
    clear_counters();
    assert(ch341a_spi_init() == 0);
    assert(config_stream(0) == 0 && last_config == 0x60);
    assert(config_stream(7) == 0 && last_config == 0x67);
    assert(config_stream(UINT_MAX) == 0 && last_config == 0x67);
    assert(ch341a_spi_shutdown() == 0);
}

int main(void)
{
    test_lifecycle_and_protocols();
    test_packet_boundaries();
    test_separate_command_and_data();
    test_arguments_and_error_propagation();
    test_initialization_cleanup();
    puts("CH341 Windows SPI: packet framing, bit order, write-echo draining, bounded batches, held CS, short/error propagation, init cleanup and SPI-only rejection passed");
    return 0;
}
