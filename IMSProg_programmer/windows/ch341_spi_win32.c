/* SPDX-License-Identifier: GPL-2.0-or-later
 * CH341A/B SPI framing for the Windows transport.
 *
 * Derived from ch341a_spi.c (flashrom/IMSProg):
 * Copyright (C) 2011 asbokid <ballymunboy@gmail.com>
 * Copyright (C) 2014 Pluto Yang <yangyj.ee@gmail.com>
 * Copyright (C) 2015-2016 Stefan Tauner
 * Copyright (C) 2015 Urja Rannikko <urjaman@gmail.com>
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 *
 * The controller owns chip select through enable_pins(). SPI transfers must
 * never change it: command, address, and data may arrive in separate calls.
 */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include "ch341a_spi.h"
#include "ch347_win32.h"

#define CH341_PACKET_LENGTH 32u
#define CH341_MAX_OUT_LENGTH 4096u
/* Reserve the zero-filled prefix packet used by the native backend. */
#define CH341_BATCH_PACKETS ((CH341_MAX_OUT_LENGTH / CH341_PACKET_LENGTH) - 1u)
#define CH341_BATCH_DATA (CH341_BATCH_PACKETS * (CH341_PACKET_LENGTH - 1u))
#define CH341_CMD_SPI_STREAM 0xa8u
#define CH341_CMD_I2C_STREAM 0xaau
#define CH341_CMD_UIO_STREAM 0xabu

static struct ch347_priv context;
static bool opened;

static int exchange(const uint8_t *tx, unsigned int tx_len,
                    uint8_t *rx, unsigned int rx_len)
{
    int ret;
    if (!opened)
        return -ENODEV;
    ret = ch341_windows_exchange(&context, tx, tx_len, rx, rx_len);
    /* The transport guarantees exact OUT/IN lengths on zero return. A byte
     * count or any other positive result is not a successful transaction. */
    return ret > 0 ? -EIO : ret;
}

static uint8_t reverse_bits(uint8_t value)
{
    value = (uint8_t)(((value >> 1) & 0x55u) | ((value << 1) & 0xaau));
    value = (uint8_t)(((value >> 2) & 0x33u) | ((value << 2) & 0xccu));
    return (uint8_t)((value >> 4) | (value << 4));
}

int config_stream(unsigned int speed)
{
    /* Bits 0-1 select I2C speed; bit 2 selects SPI data width. This command
     * does NOT select the SPI clock frequency. Init always uses single SPI. */
    const uint8_t command[] = {
        CH341_CMD_I2C_STREAM, (uint8_t)(0x60u | (speed & 7u)), 0x00u
    };
    return exchange(command, sizeof(command), NULL, 0);
}

int enable_pins(bool enable)
{
    /* Preserve native assertion timing and output directions. Deselection
     * drives CS0 high before tri-stating; never replay the CS-low command. */
    const uint8_t command[] = {
        CH341_CMD_UIO_STREAM,
        0xb7u, 0xb7u, 0xb7u, 0xb7u, 0xb7u,
        (uint8_t)(enable ? 0xb6u : 0xb7u),
        (uint8_t)(0x40u | (enable ? 0x3fu : 0x00u)), 0x20u
    };
    return exchange(command, sizeof(command), NULL, 0);
}

int ch341a_spi_send_command(unsigned int writecnt, unsigned int readcnt,
                            const unsigned char *writearr, unsigned char *readarr)
{
    uint8_t tx[CH341_MAX_OUT_LENGTH];
    uint8_t rx[CH341_BATCH_DATA];
    unsigned int write_done = 0, read_done = 0, remaining;

    if ((writecnt && !writearr) || (readcnt && !readarr))
        return -EINVAL;
    if (writecnt > UINT_MAX - readcnt)
        return -EOVERFLOW;
    if (!opened)
        return -ENODEV;
    remaining = writecnt + readcnt;
    while (remaining) {
        const unsigned int count = min(remaining, CH341_BATCH_DATA);
        const unsigned int writes = min(writecnt - write_done, count);
        const unsigned int reads = count - writes;
        unsigned int pos = CH341_PACKET_LENGTH;
        unsigned int data_pos = 0;
        int ret;

        /* The native implementation prefixes each transfer with an empty
         * 32-byte packet. It does not touch CS; retain that wire framing. */
        memset(tx, 0, CH341_PACKET_LENGTH);
        while (data_pos < count) {
            const unsigned int packet_count = min(CH341_PACKET_LENGTH - 1u,
                                                   count - data_pos);
            unsigned int i;
            tx[pos++] = CH341_CMD_SPI_STREAM;
            for (i = 0; i < packet_count; ++i, ++data_pos)
                tx[pos++] = data_pos < writes
                    ? reverse_bits(writearr[write_done + data_pos]) : 0xffu;
        }
        /* Every transmitted SPI byte produces an IN byte, including the
         * write phase. Drain all of it so replies cannot leak into next call. */
        ret = exchange(tx, pos, rx, count);
        if (ret)
            return ret;
        for (unsigned int i = 0; i < reads; ++i)
            readarr[read_done + i] = reverse_bits(rx[writes + i]);
        write_done += writes;
        read_done += reads;
        remaining -= count;
    }
    return 0;
}

int ch341a_spi_init(void)
{
    int ret;
    if (opened)
        return -EBUSY;
    memset(&context, 0, sizeof(context));
    ret = ch341_windows_open(&context);
    if (ret) {
        ch347_windows_close(&context);
        memset(&context, 0, sizeof(context));
        return ret > 0 ? -EIO : ret;
    }
    opened = true;
    ret = config_stream(3);
    if (!ret)
        ret = enable_pins(true);
    if (ret) {
        /* A failed OUT may have partially enabled pins. Make one best-effort
         * release, then always close. Return the original initialization error. */
        (void)enable_pins(false);
        ch347_windows_close(&context);
        memset(&context, 0, sizeof(context));
        opened = false;
    }
    return ret;
}

int ch341a_spi_shutdown(void)
{
    int ret;
    if (!opened)
        return -ENODEV;
    ret = enable_pins(false);
    ch347_windows_close(&context);
    memset(&context, 0, sizeof(context));
    opened = false;
    return ret;
}

int ch341a_init(uint8_t chipType, uint16_t i2cSpeed)
{
    /* The Windows implementation intentionally supports SPI protocols only.
     * This legacy argument is not a programmable CH341 SPI clock. */
    (void)i2cSpeed;
    switch (chipType) {
    case 0: case 3: case 4: case 5: case 6:
        return ch341a_spi_init();
    default:
        return -ENOTSUP;
    }
}

int ch341a_init_i2c(void)
{
    return -ENOTSUP;
}

int ch341GetDescriptor(uint8_t *buf)
{
    if (!buf)
        return -EINVAL;
    memset(buf, 0, 18);
    return -ENOTSUP;
}
