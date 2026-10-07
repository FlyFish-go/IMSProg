/* Hardware-free regression checks for the actual CH347 packet code. */
#include "ch347.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int test_mode, sends, receives;
static unsigned char wire[510];
static int wire_len;
int libusb_bulk_transfer(libusb_device_handle *dev, unsigned char ep,
    unsigned char *data, int length, int *transferred, unsigned int timeout)
{
    (void)dev; (void)timeout;
    if (ep == CH347_EPOUT) {
        assert(length <= 510);
        memcpy(wire, data, (size_t)length);
        wire_len = length;
        ++sends;
        *transferred = test_mode == 1 ? length - 1 : length;
        return 0;
    }
    ++receives;
    if (test_mode == 5) {
        assert(wire[0] == CH347_CMD_SPI_INIT);
        data[0] = wire[0]; data[1] = 1; data[2] = 0; data[3] = 0;
        *transferred = 4; return 0;
    }
    if (test_mode == 4) { data[0] = wire[0]; data[1] = data[2] = 0; *transferred = 3; return 0; }
    if (test_mode == 2) { *transferred = 2; return 0; }
    if (test_mode == 3) {
        if (receives == 1) { data[0] = CH347_CMD_SPI_RD_WR; data[1] = 4; data[2] = 0; *transferred = 3; }
        else *transferred = 0;
        return 0;
    }
    assert(wire[0] == CH347_CMD_SPI_RD_WR && wire_len <= length);
    memcpy(data, wire, (size_t)wire_len);
    for (int i = 3; i < wire_len; ++i) data[i] ^= 0xff;
    *transferred = wire_len;
    return 0;
}
static void reset(int mode) { test_mode = mode; sends = receives = 0; }
int main(void)
{
    struct ch347_priv p;
    unsigned char data[1200], saved[1200];
    memset(&p, 0, sizeof(p));
    p.handle = (libusb_device_handle *)&p;
    for (size_t i = 0; i < sizeof(data); ++i) saved[i] = data[i] = (unsigned char)i;
    reset(0);
    assert(ch347_spi_trx_full_duplex(&p, data, sizeof(data)) == 0);
    assert(sends == 3 && receives == 3);
    for (size_t i = 0; i < sizeof(data); ++i) assert(data[i] == (unsigned char)(saved[i] ^ 0xff));
    reset(1);
    assert(ch347_spi_tx(&p, saved, 4) == -EIO && receives == 0);
    reset(2);
    assert(ch347_spi_trx_full_duplex(&p, data, 4) == -EIO);
    reset(3);
    assert(ch347_spi_trx_full_duplex(&p, data, 4) == -EIO && receives == 2);
    reset(4);
    assert(ch347_spi_tx(&p, data, 4) == -EIO);
    reset(4);
    assert(ch347_commit_settings(&p) == -EIO);
    const int probe_clocks[] = {30000, 60000, 5000, 15000};
    for (size_t i = 0; i < sizeof(probe_clocks)/sizeof(probe_clocks[0]); ++i) {
        int clock = probe_clocks[i];
        reset(5);
        assert(ch347_set_spi_freq(&p, &clock) == 0);
        assert(clock > 0 && clock <= probe_clocks[i]);
        assert(p.cfg.SPI_BaudRatePrescaler <= 56 && sends == 1 && receives == 1);
    }
    const int invalid_probe_clocks[] = {1, 2, 100};
    for (size_t i = 0; i < sizeof(invalid_probe_clocks)/sizeof(invalid_probe_clocks[0]); ++i) {
        int clock = invalid_probe_clocks[i];
        reset(5);
        assert(ch347_set_spi_freq(&p, &clock) == -EINVAL);
        assert(sends == 0 && receives == 0);
    }
    puts("CH347 transport regressions: checked SPI clocks, chunk offsets, short writes, truncated headers, zero-progress reads, empty ACKs passed");
    return 0;
}
