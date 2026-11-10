/* Hardware-free regression tests, compiled against the real spi_nor_flash.c.
 * No programmer backend, USB library, or device is opened by this executable. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "spi_controller.h"
#include "snorcmd_api.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static struct {
    u8 feature[256], command[4], page_status, program_status, erase_status;
    u8 expected_programmer, mismatch_mask;
    int step, fail_at, fail_code, selected, low_count, high_count;
    int command_length, feature_writes, data_reads, data_writes, page_reads;
    int program_executes, erases, busy_reads, busy_after_operation;
    int always_busy, wrong_programmer, feature_reads[256];
    int no_full_duplex, full_duplex_calls, merged_writes;
    unsigned int last_read_size, last_write_size, last_page, last_column;
    unsigned long sleep_us;
} fake;
static unsigned int checks;
static int without_full_duplex;

/* platform_sleep.h's Unix implementation calls this renamed usleep. */
int fake_usleep(useconds_t usec)
{
    fake.sleep_us += usec;
    return 0;
}

static int io_result(u8 programmer)
{
    ++fake.step;
    if (programmer != fake.expected_programmer) fake.wrong_programmer = 1;
    return fake.step == fake.fail_at ? fake.fail_code : 0;
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(u8 programmer)
{
    CHECK(!fake.selected);
    fake.selected = 1;
    fake.command_length = 0;
    ++fake.low_count;
    return (SPI_CONTROLLER_RTN_T)io_result(programmer);
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(u8 programmer)
{
    int ret;
    CHECK(fake.selected);
    fake.selected = 0;
    ++fake.high_count;
    ret = io_result(programmer);
    if (ret) return (SPI_CONTROLLER_RTN_T)ret;
    if (fake.command_length) {
        switch (fake.command[0]) {
        case 0x1f:
            CHECK(fake.command_length == 3);
            fake.feature[fake.command[1]] = fake.command[2];
            fake.feature[fake.command[1]] ^= fake.mismatch_mask;
            ++fake.feature_writes;
            break;
        case 0x13:
            CHECK(fake.command_length == 4);
            fake.last_page = ((unsigned int)fake.command[1] << 16) |
                             ((unsigned int)fake.command[2] << 8) | fake.command[3];
            fake.feature[0xc0] = fake.page_status;
            fake.busy_reads = fake.busy_after_operation;
            ++fake.page_reads;
            break;
        case 0x10:
            CHECK(fake.command_length == 4);
            fake.feature[0xc0] = fake.program_status;
            fake.busy_reads = fake.busy_after_operation;
            ++fake.program_executes;
            break;
        case 0xd8:
            CHECK(fake.command_length == 4);
            fake.last_page = ((unsigned int)fake.command[1] << 16) |
                             ((unsigned int)fake.command[2] << 8) | fake.command[3];
            fake.feature[0xc0] = fake.erase_status;
            fake.busy_reads = fake.busy_after_operation;
            ++fake.erases;
            break;
        }
    }
    return SPI_CONTROLLER_RTN_NO_ERROR;
}

static int fake_command_length(u8 opcode)
{
    switch (opcode) {
    case 0x06: case 0xff: return 1;
    case 0x0f: case 0x9f: return 2;
    case 0x02: case 0x1f: return 3;
    default: return 4;
    }
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *data, u32 len,
                                               SPI_CONTROLLER_SPEED_T speed, u8 programmer)
{
    int ret;
    CHECK(fake.selected && speed == SPI_CONTROLLER_SPEED_SINGLE);
    ret = io_result(programmer);
    if (ret) return (SPI_CONTROLLER_RTN_T)ret;
    if (!fake.command_length) {
        int clen;
        CHECK(len > 0);
        clen = fake_command_length(data[0]);
        if ((int)len > clen) {
            /* Command and payload merged into one write. */
            CHECK(data[0] == 0x02);
            fake.last_write_size = len - (u32)clen;
            ++fake.data_writes;
            ++fake.merged_writes;
        } else {
            CHECK((int)len == clen);
        }
        memcpy(fake.command, data, (size_t)clen);
        fake.command_length = clen;
    } else {
        CHECK(fake.command[0] == 0x02 && fake.command_length == 3);
        fake.last_write_size = len;
        ++fake.data_writes;
    }
    return SPI_CONTROLLER_RTN_NO_ERROR;
}

static void fake_read_payload(u8 *data, u32 len)
{
    unsigned int i;
    switch (fake.command[0]) {
    case 0x0f:
        CHECK(len == 1 && fake.command_length == 2);
        ++fake.feature_reads[fake.command[1]];
        data[0] = fake.feature[fake.command[1]];
        if (fake.command[1] == 0xc0 && (fake.always_busy || fake.busy_reads)) {
            data[0] |= 1;
            if (fake.busy_reads) --fake.busy_reads;
        }
        break;
    case 0x9f:
        CHECK(fake.command_length == 2 && fake.command[1] == 0);
        for (i = 0; i < len; ++i) data[i] = (u8[]){0xef, 0xba, 0x21}[i % 3];
        break;
    case 0x03:
        CHECK(fake.command_length == 4 && fake.command[3] == 0);
        fake.last_column = ((unsigned int)fake.command[1] << 8) | fake.command[2];
        for (i = 0; i < len; ++i) data[i] = (u8)(i ^ fake.last_page);
        fake.last_read_size = len;
        ++fake.data_reads;
        break;
    default: CHECK(0);
    }
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *data, u32 len,
                                              SPI_CONTROLLER_SPEED_T speed, u8 programmer)
{
    int ret;
    CHECK(fake.selected && fake.command_length && speed == SPI_CONTROLLER_SPEED_SINGLE);
    ret = io_result(programmer);
    if (ret) return (SPI_CONTROLLER_RTN_T)ret;
    fake_read_payload(data, len);
    return SPI_CONTROLLER_RTN_NO_ERROR;
}

int SPI_CONTROLLER_Has_Full_Duplex(u8 programmer)
{
    (void)programmer;
    return !fake.no_full_duplex;
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Full_Duplex(u8 *buf, u32 len, u8 programmer)
{
    int ret, clen;
    CHECK(!fake.no_full_duplex);
    CHECK(fake.selected && !fake.command_length && len > 0);
    ret = io_result(programmer);
    if (ret) return (SPI_CONTROLLER_RTN_T)ret;
    clen = fake_command_length(buf[0]);
    CHECK((int)len > clen);
    memcpy(fake.command, buf, (size_t)clen);
    fake.command_length = clen;
    ++fake.full_duplex_calls;
    fake_read_payload(buf + clen, len - (u32)clen);
    return SPI_CONTROLLER_RTN_NO_ERROR;
}

static void reset_fake(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.no_full_duplex = without_full_duplex;
    fake.expected_programmer = 2;
    nand_set_ecc_status_mode(NAND_ECC_STATUS_NONE);
}

static void reset_trace(void)
{
    fake.step = fake.fail_at = fake.low_count = fake.high_count = 0;
    fake.feature_writes = fake.data_reads = fake.data_writes = fake.page_reads = 0;
    fake.program_executes = fake.erases = 0;
    fake.sleep_us = 0;
    memset(fake.feature_reads, 0, sizeof(fake.feature_reads));
}

static void check_clean_bus(void)
{
    CHECK(!fake.selected && fake.low_count == fake.high_count);
    CHECK(!fake.wrong_programmer);
    ++checks;
}

static void prepare_ecc(int enabled, int mode)
{
    u8 value;
    reset_fake();
    nand_set_ecc_status_mode(mode);
    CHECK(nand_configure_ecc(2, enabled, mode == NAND_ECC_STATUS_W25N01GW, &value) == 0);
    CHECK(((value & 0x10) != 0) == enabled);
    reset_trace();
}

static void test_configuration(void)
{
    unsigned int initial;
    int enabled, buffer;
    u8 actual;
    for (initial = 0; initial <= 255; ++initial) {
        for (enabled = 0; enabled <= 1; ++enabled) {
            for (buffer = 0; buffer <= 1; ++buffer) {
                u8 desired = (u8)((initial & ~0x10u) | (enabled ? 0x10 : 0));
                if (buffer) desired |= 0x08;
                reset_fake();
                fake.feature[0xb0] = (u8)initial;
                actual = 0x55;
                CHECK(nand_configure_ecc(2, enabled, buffer, &actual) == 0);
                CHECK(actual == desired && fake.feature[0xb0] == desired);
                CHECK(fake.feature_writes == (initial != desired));
                CHECK(fake.feature_reads[0xb0] == 2);
                check_clean_bus();
            }
        }
    }
    /* Only the target bits (ECC-E, and BUF when requested) must verify;
     * other bits may read back differently on some devices. */
    for (unsigned int bit = 1; bit <= 0x80; bit <<= 1) {
        reset_fake();
        fake.mismatch_mask = (u8)bit;
        CHECK(nand_configure_ecc(2, 1, 1, &actual) ==
              ((bit & 0x18) ? NAND_ERROR_CONFIG_VERIFY : 0));
        CHECK(actual == (u8)(0x18 ^ bit));
        check_clean_bus();
    }
}

static void test_configuration_transport(void)
{
    int count, at, code;
    reset_fake();
    CHECK(nand_configure_ecc(2, 1, 1, NULL) == 0);
    count = fake.step;
    for (code = 0; code < 2; ++code) {
        for (at = 1; at <= count; ++at) {
            reset_fake();
            fake.fail_at = at;
            fake.fail_code = code ? 3 : -7;
            CHECK(nand_configure_ecc(2, 1, 1, NULL) == (code ? NAND_ERROR_TRANSPORT : -7));
            check_clean_bus();
        }
    }
    reset_fake();
    fake.always_busy = 1;
    CHECK(nand_configure_ecc(2, 1, 1, NULL) == NAND_ERROR_TIMEOUT);
    CHECK(fake.feature_writes == 0 && fake.sleep_us == 100000);
    check_clean_bus();
}

static void test_reads_and_status(void)
{
    u8 data[2112 * 2 + 2];
    unsigned int size, status;
    int enabled;
    for (enabled = 0; enabled <= 1; ++enabled) {
        for (size = 2048; size <= 2112; size += 64) {
            for (status = 0; status <= 3; ++status) {
                prepare_ecc(enabled, NAND_ECC_STATUS_W25N01GW);
                memset(data, 0x55, sizeof(data));
                fake.page_status = (u8)(status << 4);
                int ret = nand_block_read(data + 1, size, 3, 2, 2);
                /* Uncorrectable pages are recorded but never abort a read. */
                CHECK(ret == 0);
                CHECK(fake.feature_writes == 0); /* Read never toggles ECC. */
                CHECK(data[0] == 0x55 && data[size * 2 + 1] == 0x55);
                CHECK(fake.data_reads == 2 && fake.last_read_size == size);
                CHECK(fake.last_page == 7 && fake.last_column == 0);
                CHECK(data[1] == 6 && data[size + 1] == 7);
                unsigned int flags = !enabled || !status ? NAND_ECC_READ_CLEAN :
                    status == 1 ? NAND_ECC_READ_CORRECTED : NAND_ECC_READ_UNCORRECTABLE;
                CHECK(nand_get_ecc_read_status() == flags);
                CHECK(nand_get_ecc_uncorrectable_pages() ==
                      (flags == NAND_ECC_READ_UNCORRECTABLE ? 2u : 0u));
                CHECK(nand_get_ecc_corrected_pages() ==
                      (flags == NAND_ECC_READ_CORRECTED ? 2u : 0u));
                if (flags == NAND_ECC_READ_UNCORRECTABLE) {
                    u32 pages[4] = {0};
                    CHECK(nand_get_ecc_uncorrectable_page_list(pages, 4) == 2);
                    CHECK(pages[0] == 6 && pages[1] == 7);
                }
                check_clean_bus();
            }
        }
    }
    /* Other vendors' status bits must not be decoded as Winbond ECC. */
    prepare_ecc(1, NAND_ECC_STATUS_NONE);
    fake.page_status = 0x30;
    CHECK(nand_page_read(data, 2048, 0) == 0);
    CHECK(nand_get_ecc_read_status() == NAND_ECC_READ_CLEAN);
    /* Corrected status survives subsequent clean pages and blocks. */
    prepare_ecc(1, NAND_ECC_STATUS_W25N01GW);
    fake.page_status = 0x10;
    CHECK(nand_block_read(data, 2048, 0, 1, 2) == 0);
    fake.page_status = 0;
    CHECK(nand_block_read(data, 2048, 1, 1, 2) == 0);
    CHECK(nand_get_ecc_read_status() == NAND_ECC_READ_CORRECTED);
    fake.page_status = 0x20;
    CHECK(nand_page_read(data, 2048, 0) == 0);
    CHECK(nand_get_ecc_read_status() == (NAND_ECC_READ_CORRECTED | NAND_ECC_READ_UNCORRECTABLE));
    nand_reset_ecc_read_status();
    CHECK(nand_get_ecc_read_status() == NAND_ECC_READ_CLEAN);
    check_clean_bus();
}

static void test_vendor_decoding(void)
{
    /* Winbond: SR[5:4] 00/01/10/11. */
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_WINBOND, 0x00) == NAND_ECC_READ_CLEAN);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_WINBOND, 0x10) == NAND_ECC_READ_CORRECTED);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_WINBOND, 0x20) == NAND_ECC_READ_UNCORRECTABLE);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_WINBOND, 0x30) == NAND_ECC_READ_UNCORRECTABLE);
    /* Micron: SR[6:4], 010 uncorrectable, 001/011/101 corrected. */
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_MICRON, 0x00) == NAND_ECC_READ_CLEAN);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_MICRON, 0x10) == NAND_ECC_READ_CORRECTED);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_MICRON, 0x20) == NAND_ECC_READ_UNCORRECTABLE);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_MICRON, 0x30) == NAND_ECC_READ_CORRECTED);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_MICRON, 0x50) == NAND_ECC_READ_CORRECTED);
    /* Generic (GigaDevice/Macronix/Kioxia...): 10 uncorrectable, 11 corrected. */
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_GENERIC, 0x20) == NAND_ECC_READ_UNCORRECTABLE);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_GENERIC, 0x30) == NAND_ECC_READ_CORRECTED);
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_GENERIC, 0x0c) == NAND_ECC_READ_CLEAN);
    /* Busy/WEL/fail bits never affect decoding. */
    CHECK(nand_decode_ecc_status(NAND_ECC_STATUS_WINBOND, 0x0f) == NAND_ECC_READ_CLEAN);
    CHECK(nand_ecc_status_mode_for_id(0xef) == NAND_ECC_STATUS_WINBOND);
    CHECK(nand_ecc_status_mode_for_id(0x2c) == NAND_ECC_STATUS_MICRON);
    CHECK(nand_ecc_status_mode_for_id(0xc8) == NAND_ECC_STATUS_GENERIC);
    CHECK(nand_ecc_status_mode_for_id(0xff) == NAND_ECC_STATUS_NONE);
    /* Generic mode decodes during a real page read. */
    {
        u8 data[2048];
        prepare_ecc(1, NAND_ECC_STATUS_GENERIC);
        fake.page_status = 0x20;
        CHECK(nand_page_read(data, 2048, 5) == 0);
        CHECK(nand_get_ecc_uncorrectable_pages() == 1);
        check_clean_bus();
    }
}

static void test_read_transport(void)
{
    u8 data[2112 * 2];
    int count, at, code;
    prepare_ecc(1, NAND_ECC_STATUS_W25N01GW);
    CHECK(nand_page_read(data, 2048, 0) == 0);
    count = fake.step;
    for (code = 0; code < 2; ++code) {
        for (at = 1; at <= count; ++at) {
            prepare_ecc(1, NAND_ECC_STATUS_W25N01GW);
            fake.fail_at = at;
            fake.fail_code = code ? 2 : -9;
            CHECK(nand_block_read(data, 2112, 0, 2, 2) == (code ? NAND_ERROR_TRANSPORT : -9));
            CHECK(fake.page_reads <= 1 && fake.feature_writes == 0);
            check_clean_bus();
        }
    }
    prepare_ecc(0, NAND_ECC_STATUS_W25N01GW);
    /* 100 ms timeout at 100 us per poll: 1001 samples, all busy. */
    fake.busy_after_operation = 1001;
    CHECK(nand_page_read(data, 2048, 0) == NAND_ERROR_TIMEOUT);
    CHECK(fake.data_reads == 0);
    check_clean_bus();
}

static void test_identification(void)
{
    int at, code, count;
    u8 id[3];
    reset_fake();
    CHECK(nand_read_devid(id, 3, 2) == 0);
    CHECK(id[0] == 0xef && id[1] == 0xba && id[2] == 0x21);
    count = fake.step; /* 3 with full duplex, 4 with separate write/read. */
    CHECK(count == (without_full_duplex ? 4 : 3));
    for (code = 0; code < 2; ++code) {
        for (at = 1; at <= count; ++at) {
            reset_fake();
            fake.fail_at = at;
            fake.fail_code = code ? 1 : -8;
            CHECK(nand_read_devid(id, 3, 2) == (code ? NAND_ERROR_TRANSPORT : -8));
            check_clean_bus();
        }
    }
}

static void test_writes_and_erases(void)
{
    u8 data[2112 * 2] = {0};
    int count, at, code;
    for (unsigned int size = 2048; size <= 2112; size += 64) {
        reset_fake();
        fake.busy_after_operation = 2;
        CHECK(nand_block_write(data, size, 0, 2, 2) == 0);
        CHECK(fake.data_writes == 2 && fake.last_write_size == size);
        CHECK(fake.program_executes == 2 && fake.busy_reads == 0);
        check_clean_bus();
    }
    reset_fake();
    fake.program_status = 0x08;
    CHECK(nand_block_write(data, 2048, 0, 2, 2) == NAND_ERROR_PROGRAM_FAIL);
    CHECK(fake.program_executes == 1);
    check_clean_bus();
    reset_fake();
    CHECK(nand_block_write(data, 2048, 0, 1, 2) == 0);
    count = fake.step;
    for (code = 0; code < 2; ++code) {
        for (at = 1; at <= count; ++at) {
            reset_fake();
            fake.fail_at = at;
            fake.fail_code = code ? 3 : -6;
            CHECK(nand_block_write(data, 2048, 0, 2, 2) == (code ? NAND_ERROR_TRANSPORT : -6));
            CHECK(fake.program_executes <= 1);
            check_clean_bus();
        }
    }
    reset_fake();
    fake.expected_programmer = 3;
    fake.feature[0xa0] = 0xff;
    fake.busy_after_operation = 2;
    CHECK(nand_block_erase(17, 64, 3) == 0);
    CHECK(fake.feature[0xa0] == 0x83 && fake.last_page == 17 * 64);
    CHECK(fake.erases == 1 && fake.busy_reads == 0);
    check_clean_bus();
    reset_fake();
    fake.erase_status = 0x04;
    CHECK(nand_block_erase(0, 64, 2) == NAND_ERROR_ERASE_FAIL);
    check_clean_bus();
    reset_fake();
    fake.feature[0xa0] = 0x7c;
    CHECK(nand_block_erase(0, 64, 2) == 0);
    count = fake.step;
    for (code = 0; code < 2; ++code) {
        for (at = 1; at <= count; ++at) {
            reset_fake();
            fake.feature[0xa0] = 0x7c;
            fake.fail_at = at;
            fake.fail_code = code ? 3 : -5;
            CHECK(nand_block_erase(0, 64, 2) == (code ? NAND_ERROR_TRANSPORT : -5));
            check_clean_bus();
        }
    }
}

static void test_unprotect_and_invalid_arguments(void)
{
    u8 data[2112];
    reset_fake();
    fake.feature[0xa0] = 0xff;
    fake.mismatch_mask = 4;
    CHECK(nand_unprotect(2) == NAND_ERROR_CONFIG_VERIFY);
    CHECK(fake.feature[0xa0] == 0x87);
    check_clean_bus();
    reset_fake();
    CHECK(nand_page_read(NULL, 2048, 0) == NAND_ERROR_INVALID_ARGUMENT);
    CHECK(nand_page_write(data, 0, 0) == NAND_ERROR_INVALID_ARGUMENT);
    CHECK(nand_page_read(data, 2048, 0x1000000) == NAND_ERROR_INVALID_ARGUMENT);
    CHECK(nand_block_read(data, 2048, 0, 0, 2) == NAND_ERROR_INVALID_ARGUMENT);
    CHECK(nand_block_write(data, 2048, 0xffffff, 64, 2) == NAND_ERROR_INVALID_ARGUMENT);
    CHECK(nand_block_erase(0x1000000, 64, 2) == NAND_ERROR_INVALID_ARGUMENT);
    CHECK(nand_read_devid(NULL, 3, 2) == NAND_ERROR_INVALID_ARGUMENT);
    CHECK(nand_checkBadBlock(0, 65536, 64, 2) == -1);
    CHECK(fake.step == 0);
    check_clean_bus();
}

static void test_waits_and_badblocks(void)
{
    u8 id[3];
    int count, at;
    reset_fake();
    CHECK(nand_read_devid(id, 3, 2) == 0); /* Set the active programmer. */
    reset_trace();
    fake.always_busy = 1;
    CHECK(nand_wait_ready(1) == NAND_ERROR_TIMEOUT);
    CHECK(fake.feature_reads[0xc0] == 11 && fake.sleep_us == 1000);
    CHECK(nand_wait_ready(-1) == NAND_ERROR_INVALID_ARGUMENT);
    check_clean_bus();
    reset_fake();
    /* Fake byte value equals page modulo 256. Pick FF for a good marker. */
    CHECK(nand_checkBadBlock(255, 2048, 1, 2) == 0);
    CHECK(fake.last_column == 2048 && fake.last_read_size == 2);
    count = fake.step;
    reset_fake();
    CHECK(nand_checkBadBlock(0, 2048, 64, 2) == 1);
    for (at = 1; at <= count; ++at) {
        reset_fake();
        fake.fail_at = at;
        fake.fail_code = 1; /* Positive controller failure must not mean bad. */
        CHECK(nand_checkBadBlock(0, 2048, 64, 2) == -1);
        check_clean_bus();
    }
}

static void test_transfer_batching(void)
{
    static unsigned char data[2 * 2112];
    u8 id[3];
    reset_fake();
    CHECK(nand_read_devid(id, 3, 2) == 0);
    CHECK(fake.full_duplex_calls == (without_full_duplex ? 0 : 1));
    CHECK(id[0] == 0xef && id[1] == 0xba && id[2] == 0x21);
    reset_trace();
    fake.full_duplex_calls = 0;
    memset(fake.feature_reads, 0, sizeof(fake.feature_reads));
    CHECK(nand_block_write(data, 2112, 0, 2, 2) == 0);
    /* One write per page carries command and payload together. */
    CHECK(fake.merged_writes == 2 && fake.data_writes == 2 && fake.last_write_size == 2112);
    CHECK(fake.program_executes == 2);
    /* Ready poll before page 0, one after each program; none before page 1. */
    CHECK(fake.feature_reads[0xc0] == 3);
    CHECK(fake.full_duplex_calls == (without_full_duplex ? 0 : 3));
    reset_trace();
    CHECK(nand_block_read(data, 2112, 0, 2, 2) == 0);
    CHECK(fake.page_reads == 2 && fake.data_reads == 2 && fake.feature_reads[0xc0] == 3);
    /* All-FF pages are skipped; the first programmed page still pre-waits. */
    memset(data, 0xff, sizeof(data));
    data[2112 + 7] = 0x00;          /* page 1 has data, page 0 is blank */
    reset_trace();
    CHECK(nand_block_write(data, 2112, 0, 2, 2) == 0);
    CHECK(fake.program_executes == 1 && fake.data_writes == 1);
    CHECK(fake.feature_reads[0xc0] == 2);
    memset(data, 0xff, sizeof(data));
    reset_trace();
    CHECK(nand_block_write(data, 2112, 0, 2, 2) == 0);
    CHECK(fake.program_executes == 0 && fake.step == 0);
    memset(data, 0, sizeof(data));
    reset_trace();
    CHECK(nand_page_write(data, 2112, 5) == 0);
    CHECK(fake.feature_reads[0xc0] == 2); /* Standalone page keeps its pre-wait. */
    check_clean_bus();
}

int main(void)
{
    test_configuration();
    test_configuration_transport();
    test_reads_and_status();
    test_vendor_decoding();
    test_read_transport();
    test_identification();
    test_writes_and_erases();
    test_waits_and_badblocks();
    test_unprotect_and_invalid_arguments();
    test_transfer_batching();
    if (!without_full_duplex) {
        /* Repeat everything on a programmer without full-duplex support. */
        without_full_duplex = 1;
        return main();
    }
    printf("PASS: NAND core fake-SPI regression (%u checked cases)\n", checks);
    return 0;
}
