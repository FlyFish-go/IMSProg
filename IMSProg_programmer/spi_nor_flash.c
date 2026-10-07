/*
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * 2023-2026 Mikhail Medvedev <e-ink-reader@yandex.ru>
 * spi_nor_flash.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "platform_sleep.h"
#include <stdbool.h>
#include "spi_controller.h"
#include "snorcmd_api.h"
#include "types.h"
#include "timer.h"

#define min(a,b) (((a)<(b))?(a):(b))

/******************************************************************************
 * SPI FLASH elementray definition and function
 ******************************************************************************/

#define FLASH_PAGESIZE			256

/* Flash opcodes. */
#define OPCODE_WREN			6	/* Write enable */
#define OPCODE_WRDI			4	/* Write disable */
#define OPCODE_RDSR			5	/* Read status register */
#define OPCODE_WRSR			1	/* Write status register */
#define OPCODE_READ			3	/* Read data bytes */
#define OPCODE_PP			2	/* Page program */
#define OPCODE_SE			0xD8	/* Sector erase */
#define OPCODE_RES			0xAB	/* Read Electronic Signature */
#define OPCODE_RDID			0x9F	/* Read JEDEC ID */

#define OPCODE_FAST_READ		0x0B	/* Fast Read */
#define OPCODE_DOR			0x3B	/* Dual Output Read */
#define OPCODE_QOR			0x6B	/* Quad Output Read */
#define OPCODE_DIOR			0xBB	/* Dual IO High Performance Read */
#define OPCODE_QIOR			0xEB	/* Quad IO High Performance Read */
#define OPCODE_READ_ID			0x90	/* Read Manufacturer and Device ID */

#define OPCODE_P4E			0x20	/* 4KB Parameter Sectore Erase */
#define OPCODE_P8E			0x40	/* 8KB Parameter Sectore Erase */
#define OPCODE_BE			0x60	/* Bulk Erase */
#define OPCODE_BE1			0xC7	/* Bulk Erase */
#define OPCODE_QPP			0x32	/* Quad Page Programing */

#define OPCODE_CLSR			0x30
#define OPCODE_RCR			0x35	/* Read Configuration Register */

#define OPCODE_BRRD			0x16
#define OPCODE_BRWR			0x17

/* Status Register bits. */
#define SR_WIP				1	/* Write in progress */
#define SR_WEL				2	/* Write enable latch */
#define SR_BP0				4	/* Block protect 0 */
#define SR_BP1				8	/* Block protect 1 */
#define SR_BP2				0x10	/* Block protect 2 */
#define SR_EPE				0x20	/* Erase/Program error */
#define SR_SRWD				0x80	/* SR write protect */

#define snor_dbg(args...)
/* #define snor_dbg(args...) do { if (1) printf(args); } while(0) */


struct {
  //AT45DBxxx chips have a non-standard sector size of 528 bytes, plus a few bytes for write commands, making a total of 532–534 bytes. 548 should be more then enough
  uint8_t ibuf[548];
  uint8_t obuf[548];
} spi_buf;


#define udelay(x)			imsprog_usleep(x)
u8 programmerType = 0;
/*
struct chip_info {
        char		*name;
        u8		id;
        u32		jedec_id;
        unsigned long	sector_size;
        unsigned int	n_sectors;
        char		addr4b;
};
*/
unsigned char algType = 0;
//struct chip_info *spi_chip_info;

static int snor_read_sr(u8 *val);
static int snor_write_sr(u8 *val);
static int s95_read_sr(u8 *val);
static int s95_write_sr(u8 *val);
int s95_wait_ready(int sleep_ms);
int s95_unprotect(void);

extern unsigned int bsize;

/*
 * Set write enable latch with Write Enable command.
 * Returns negative if error occurred.
 */
static inline void snor_write_enable(void)
{
    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(OPCODE_WREN, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
}

static inline void snor_write_disable(void)
{
    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(OPCODE_WRDI, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
}
static inline void s95_write_enable(void)
{
    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x06, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
}

static inline void s95_write_disable(void)
{
    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x04, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
}
/*
 * Set all sectors (global) unprotected if they are protected.
 * Returns negative if error occurred.
 */
static inline int snor_unprotect(void)
{
        u8 sr = 0;

        if (snor_read_sr(&sr) < 0) {
                printf("%s: read_sr fail: %x\n", __func__, sr);
                return -1;
        }

        if ((sr & (SR_BP0 | SR_BP1 | SR_BP2)) != 0) {
                sr = 0;
                snor_write_sr(&sr);
        }
        return 0;
}

/*
 * Service routine to read status register until ready, or timeout occurs.
 * Returns non-zero if error.
 */
int snor_wait_ready(int sleep_ms)
{
        int count;
        int sr = 0;

        /* one chip guarantees max 5 msec wait here after page writes,
         * but potentially three seconds (!) after page erase.
         */
        for (count = 0; count < ((sleep_ms + 1) * 1000); count++) {
                if ((snor_read_sr((u8 *)&sr)) < 0)
                        break;
                else if (!(sr & (SR_WIP | SR_EPE | SR_WEL))) {
                        return 0;
                }
                udelay(500);
                /* REVISIT sometimes sleeping would be best */
        }
        printf("%s: read_sr fail: %x\n", __func__, sr);
    return -1;
}

/*
 * read status register
 */
static int snor_read_rg(u8 code, u8 *val)
{
        int retval;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(code, programmerType);
    retval = SPI_CONTROLLER_Read_NByte(val, 1, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
        if (retval) {
                printf("%s: ret: %x\n", __func__, retval);
                return -1;
        }

        return 0;
}

/*
 * write status register
 */
static int snor_write_rg(u8 code, u8 *val)
{
        int retval;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(code, programmerType);
    retval = SPI_CONTROLLER_Write_NByte(val, 1, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
        if (retval) {
                printf("%s: ret: %x\n", __func__, retval);
                return -1;
        }

        return 0;
}

static int snor_4byte_mode(int enable)
{
        int retval;

        if (snor_wait_ready(1))
                return -1;

    //if (spi_chip_info->id == 0x1) /* Spansion */
    if (algType == 0x02) /* Spansion */
        {
                u8 br_cfn;
                u8 br = enable ? 0x81 : 0;

                snor_write_rg(OPCODE_BRWR, &br);
                snor_read_rg(OPCODE_BRRD, &br_cfn);
                if (br_cfn != br) {
                        printf("4B mode switch failed %s, 0x%02x, 0x%02x\n", enable ? "enable" : "disable" , br_cfn, br);
                        return -1;
                }
        } else {
                u8 code = enable ? 0xb7 : 0xe9; /* B7: enter 4B, E9: exit 4B */

        SPI_CONTROLLER_Chip_Select_Low(programmerType);
        retval = SPI_CONTROLLER_Write_One_Byte(code, programmerType);
        SPI_CONTROLLER_Chip_Select_High(programmerType);
                if (retval) {
                        printf("%s: ret: %x\n", __func__, retval);
                        return -1;
                }
        //if ((!enable) && (spi_chip_info->id == 0xef)) /* Winbond */
        if ((!enable) && (algType == 0x01)) /* Winbond */
                {
                        code = 0;
                        snor_write_enable();
                        snor_write_rg(0xc5, &code);
                }
        }
        return 0;
}


int full_erase_chip(void)
{
        timer_start();
        /* Wait until finished previous write command. */
        if (snor_wait_ready(3))
                return -1;

        /* Send write enable, then erase commands. */
        snor_write_enable();
        snor_unprotect();

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(OPCODE_BE1, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);

        snor_wait_ready(950);
        snor_write_disable();
        timer_end();

        return 0;
}

int snor_block_erase(unsigned int sector_number, unsigned int blockSize, u8 addr4b, u8 progType)
{
    uint8_t *ptr = spi_buf.obuf;
    unsigned int physical_addr;
    //addr4bit transforming
    algType = (addr4b & 0xf0) >> 4;
    addr4b = addr4b & 0x0f;

    programmerType = progType;

    if (addr4b) snor_4byte_mode(1);

    physical_addr = sector_number * blockSize;
    /* Wait until finished previous write command. */
    if (snor_wait_ready(950)) return -1;
    snor_write_enable();

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    *ptr++ = 0xd8;
    if (addr4b) *ptr++ = (physical_addr >> 24) & 0xff;
    *ptr++ = (physical_addr >> 16) & 0xff;
    *ptr++ = (physical_addr >> 8) & 0xff;
    *ptr++ = physical_addr & 0xff;
    if (addr4b) SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 5, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    else SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 4, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);

    snor_write_disable();
    //if (addr4b) snor_4byte_mode(0);
    return 0;
}

/*
 * read SPI flash device ID
 */
int snor_read_devid(u8 *rxbuf, int n_rx, uint8_t progType)
{
        int retval = 0;
    programmerType = progType;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(OPCODE_RDID, programmerType);

    retval = SPI_CONTROLLER_Read_NByte(rxbuf, n_rx, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);

        if (retval) {
                printf("%s: ret: %x\n", __func__, retval);
                return retval;
        }

    if ((rxbuf[0] == 0) && (rxbuf[1] == 0) && (rxbuf[2] == 0))
    {
        rxbuf[0] = 0xff;
        rxbuf[1] = 0xff;
        rxbuf[2] = 0xff;
    }

        return 0;
}

/*
 * read status register
 */
static int snor_read_sr(u8 *val)
{
        int retval = 0;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(OPCODE_RDSR, programmerType);

    retval = SPI_CONTROLLER_Read_NByte(val, 1, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
        if (retval) {
                printf("%s: ret: %x\n", __func__, retval);
                return retval;
        }

        return 0;
}

/*
 * write status register
 */
static int snor_write_sr(u8 *val)
{
        int retval = 0;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(OPCODE_WRSR, programmerType);

    retval = SPI_CONTROLLER_Write_NByte(val, 1, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
        if (retval) {
                printf("%s: ret: %x\n", __func__, retval);
                return retval;
        }
        return 0;
}


int snor_read_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned int addr4b, uint8_t progType)
{
    u32 read_addr, physical_read_addr, remain_len, data_offset;
    programmerType = progType;
    //addr4bit transforming
    algType = (addr4b & 0xf0) >> 4;
    addr4b = addr4b & 0x0f;
    uint8_t *ptr = spi_buf.obuf;

    snor_dbg("%s: from:%x len:%x \n", __func__, from, len);

    /* sanity checks */
    if (len == 0)
        return 0;

    /* Wait till previous write/erase is done. */
    if (snor_wait_ready(1)) {
        /* REVISIT status return?? */
        return -1;
    }

    read_addr = from;
    remain_len = len;

    while(remain_len > 0) {

        physical_read_addr = read_addr;
        data_offset = (physical_read_addr % (sector_size));

        if (addr4b)
            snor_4byte_mode(1);

        SPI_CONTROLLER_Chip_Select_Low(programmerType);

        /* Set up the write data buffer. */
        *ptr++ = OPCODE_READ;
        if (addr4b) *ptr++ = (physical_read_addr >> 24) & 0xff;
        *ptr++ = (physical_read_addr >> 16) & 0xff;
        *ptr++ = (physical_read_addr >> 8) & 0xff;
        *ptr++ = physical_read_addr & 0xff;
        if (addr4b) SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 5, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
        else SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 4, SPI_CONTROLLER_SPEED_SINGLE, programmerType);

        if( (data_offset + remain_len) < sector_size )
        {
            if(SPI_CONTROLLER_Read_NByte(&buf[len - remain_len], remain_len, SPI_CONTROLLER_SPEED_SINGLE, programmerType)) {
                SPI_CONTROLLER_Chip_Select_High(programmerType);
                if (addr4b)
                    snor_4byte_mode(0);
                len = -1;
                break;
            }
            remain_len = 0;
        } else {
            if(SPI_CONTROLLER_Read_NByte(&buf[len - remain_len], sector_size - data_offset, SPI_CONTROLLER_SPEED_SINGLE, programmerType)) {
                SPI_CONTROLLER_Chip_Select_High(programmerType);
                if (addr4b)
                    snor_4byte_mode(0);
                len = -1;
                break;
            }
            remain_len -= sector_size - data_offset;
            read_addr += sector_size - data_offset;

        }

        SPI_CONTROLLER_Chip_Select_High(programmerType);

        if (addr4b)
            snor_4byte_mode(0);
    }

    return len;
}

int snor_write_param(unsigned char *buf, unsigned long to, unsigned long len, unsigned int sector_size, unsigned int addr4b, u8 progType)
{
    u32 page_size;
    int ret =0;

    programmerType = progType;

    //addr4bit transforming
    algType = (addr4b & 0xf0) >> 4;
    addr4b = addr4b & 0x0f;

    snor_dbg("%s: to:%x len:%x \n", __func__, to, len);

    /* sanity checks */
    if (len == 0) return 0;

    /* Wait until finished previous write command. */
    if (snor_wait_ready(2)) {
        return -1;
    }

    if (addr4b) snor_4byte_mode(1);

    /* write everything in PAGESIZE chunks */
    while (len > 0) {
        uint8_t *ptr = spi_buf.obuf;
        page_size = min(len, FLASH_PAGESIZE);
        /* write the next page to flash */

        snor_wait_ready(3);
        snor_write_enable();

        SPI_CONTROLLER_Chip_Select_Low(programmerType);
        /* Set up the opcode in the write buffer. */

        *ptr++ = OPCODE_PP;
        if (addr4b) *ptr++ = (to >> 24) & 0xff;
        *ptr++ = (to >> 16) & 0xff;
        *ptr++ = (to >> 8) & 0xff;
        *ptr++ = to & 0xff;
        memcpy(ptr, buf, page_size);
        if (addr4b) ret = SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 5 + page_size, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
        else ret = SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 4 + page_size, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
        SPI_CONTROLLER_Chip_Select_High(programmerType);
        if (ret != 0) return -1;
        snor_dbg("%s: to:%x page_size:%x ret:%x\n", __func__, to, page_size, rc);

        len -= page_size;
        to += page_size;
        buf += page_size;
    }

    if (addr4b) snor_4byte_mode(0);

    snor_write_disable();

    return 1;
}

int snorUnprotect(u8 progType)
{
    int ret;
    uint8_t buf[2];

    programmerType = progType;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x05, programmerType);
    ret = SPI_CONTROLLER_Read_NByte(&buf[0],2,SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
    imsprog_usleep(1);

    buf[0] = buf[0] & 0xc3; //clear bp0 - bp3

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x06, programmerType); //Write Enable
    SPI_CONTROLLER_Chip_Select_High(programmerType);
    imsprog_usleep(1);

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x01, programmerType);
    SPI_CONTROLLER_Write_One_Byte(buf[0], programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
    imsprog_usleep(1);

    return ret;
}

int s95_read_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType)
{
    u32 physical_read_addr, remain_len, data_offset;
    u32 read_addr;
    unsigned char algorythm = currentAlgorithm & 0x0f;
    unsigned char a8 = currentAlgorithm & 0x10;
    snor_dbg("%s: from:%x len:%x \n", __func__, from, len);

    programmerType = progType;

    /* sanity checks */
    if (len == 0)
        return 0;

    /* Wait till previous write/erase is done. */
    if (s95_wait_ready(1)) {
        /* REVISIT status return?? */
        return -1;
    }

    read_addr = (u32)from;
    remain_len = (u32)len;

    while(remain_len > 0) {

        physical_read_addr = read_addr;
        data_offset = (physical_read_addr % (sector_size));


        SPI_CONTROLLER_Chip_Select_Low(programmerType);

        /* Set up the write data buffer. */
        if ((from > 255) && (a8 > 0)) SPI_CONTROLLER_Write_One_Byte(0x0b, programmerType); //read command + a8 bit
        else SPI_CONTROLLER_Write_One_Byte(0x03, programmerType); //read command

        if (algorythm == 2) SPI_CONTROLLER_Write_One_Byte((physical_read_addr >> 16) & 0xff, programmerType);
        if (algorythm > 0)  SPI_CONTROLLER_Write_One_Byte((physical_read_addr >> 8) & 0xff, programmerType);
        SPI_CONTROLLER_Write_One_Byte(physical_read_addr & 0xff, programmerType);

        if( (data_offset + remain_len) < sector_size )
        {
            if(SPI_CONTROLLER_Read_NByte(&buf[len - remain_len], remain_len, SPI_CONTROLLER_SPEED_SINGLE, programmerType)) {
                SPI_CONTROLLER_Chip_Select_High(programmerType);
                len = -1;
                break;
            }
            remain_len = 0;
        } else {
            if(SPI_CONTROLLER_Read_NByte(&buf[len - remain_len], sector_size - data_offset, SPI_CONTROLLER_SPEED_SINGLE, programmerType)) {
                SPI_CONTROLLER_Chip_Select_High(programmerType);
                len = -1;
                break;
            }
            remain_len -= sector_size - data_offset;
            read_addr += sector_size - data_offset;
        }

        SPI_CONTROLLER_Chip_Select_High(programmerType);

    }

    return len;
}

int s95_write_param(unsigned char *buf, unsigned long to, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType)
{
    u32 page_offset, page_size;
    int rc = 0, retlen = 0;
    unsigned long plen = len;
    unsigned char algorythm = currentAlgorithm & 0x0f;
    unsigned char a8 = currentAlgorithm & 0x10;
    snor_dbg("%s: to:%x len:%x \n", __func__, to, len);

    programmerType = progType;

    /* sanity checks */
    if (len == 0) return 0;

    /* Wait until finished previous write command. */
    if (s95_wait_ready(2)) return -1;

    /* what page do we start with? */
    page_offset = to % sector_size;//FLASH_PAGESIZE;

    /* write everything in PAGESIZE chunks */
    while (len > 0) {
        page_size = min(len, sector_size - page_offset);
        page_offset = 0;
        /* write the next page to flash */

        s95_wait_ready(3);
        s95_write_enable();
        s95_unprotect();

        SPI_CONTROLLER_Chip_Select_Low(programmerType);
        /* Set up the opcode in the write buffer. */
        if ((to > 255) && (a8 > 0)) SPI_CONTROLLER_Write_One_Byte(0x0a, programmerType); //write command + a8 bit
        else SPI_CONTROLLER_Write_One_Byte(0x02, programmerType); //write command

        if (algorythm == 2) SPI_CONTROLLER_Write_One_Byte((to >> 16) & 0xff, programmerType);
        if (algorythm > 0) SPI_CONTROLLER_Write_One_Byte((to >> 8) & 0xff, programmerType);
        SPI_CONTROLLER_Write_One_Byte(to & 0xff, programmerType);

        if(!SPI_CONTROLLER_Write_NByte(buf, page_size, SPI_CONTROLLER_SPEED_SINGLE, programmerType))
            rc = page_size;
        else
            rc = 1;

        SPI_CONTROLLER_Chip_Select_High(programmerType);

        snor_dbg("%s: to:%x page_size:%x ret:%x\n", __func__, to, page_size, rc);

        if (rc > 0) {
            retlen += rc;
            if (rc < page_size) {
                printf("%s: rc:%x page_size:%x\n",
                        __func__, rc, page_size);
                s95_write_disable();
                return retlen - rc;
            }
        }

        len -= page_size;
        to += page_size;
        buf += page_size;
    }


    s95_write_disable();


    return retlen;
}


int s95_full_erase(u8 progType)
{
    programmerType = progType;

    timer_start();
    /* Wait until finished previous write command. */
    if (s95_wait_ready(3))
        return -1;

    /* Send write enable, then erase commands. */
    s95_write_enable();
    s95_unprotect();

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x62, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);

    s95_wait_ready(950);
    s95_write_disable();
    timer_end();

    return 0;

}


static int s95_read_sr(u8 *val)
{
    int retval = 0;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x05, programmerType);
    retval = SPI_CONTROLLER_Read_NByte(val, 1, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
    if (retval) {
        printf("%s: ret: %x\n", __func__, retval);
        return retval;
    }

    return 0;
}
static int s95_write_sr(u8 *val)
{
    int retval = 0;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x01, programmerType);

    retval = SPI_CONTROLLER_Write_NByte(val, 1, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
    if (retval) {
        printf("%s: ret: %x\n", __func__, retval);
        return retval;
    }
    return 0;
}
int s95_unprotect(void)
{
    u8 sr = 0;

    if (s95_read_sr(&sr) < 0) {
        printf("%s: read_sr fail: %x\n", __func__, sr);
        return -1;
    }

    if ((sr & (SR_BP0 | SR_BP1 )) != 0) {
        sr = 0;
        s95_write_sr(&sr);
    }
    return 0;
}

int s95_wait_ready(int sleep_ms)
{
    int count;
    int sr = 0;

    /* one chip guarantees max 5 msec wait here after page writes,
     * but potentially three seconds (!) after page erase.
     */
    for (count = 0; count < ((sleep_ms + 1) * 1000); count++) {
        //printf("sr= %x\n",snor_read_sr((u8 *)&sr));
        if ((snor_read_sr((u8 *)&sr)) < 0)
            break;
        else if (!(sr & (SR_WIP | SR_WEL))) {
            return 0;
        }
        udelay(500);
        /* REVISIT sometimes sleeping would be best */
    }
    printf("%s: read_sr fail: %x\n", __func__, sr);
    return -1;
}

int at45_read_sr(u8 *val)
{
    int retval = 0;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0xd7, programmerType);
    retval = SPI_CONTROLLER_Read_NByte(val, 1, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);
    if (retval) {
        //printf("%s: ret: %x\n", __func__, retval);
        return retval;
    }

    return 0;
}

int at45_wait_ready(int sleep_ms)
{
    int count;
    int sr = 0;

    /* one chip guarantees max 5 msec wait here after page writes,
     * but potentially three seconds (!) after page erase.
     */
    for (count = 0; count < ((sleep_ms + 1) * 1000); count++) {
        //printf("sr= %x\n",snor_read_sr((u8 *)&sr));
        if ((at45_read_sr((u8 *)&sr)) < 0)
            break;
        else if ((sr & 0x80)) {
            return 0;
        }
        udelay(500);
        /* REVISIT sometimes sleeping would be best */
    }
    printf("%s: read_sr fail: %x\n", __func__, sr);
    return -1;
}

int at45_read_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType)
{
    u32 physical_read_addr, remain_len, data_offset;
    u32 read_addr;
    unsigned char algorythm = (currentAlgorithm & 0xf0) >> 8;
    unsigned char addrLen = 9;
    unsigned int sector_addr = 0;
    int retval;
    uint8_t *ptr = spi_buf.obuf;

    programmerType = progType;

    if (sector_size > 511) addrLen++;

    /* sanity checks */
    if (len == 0)
        return 0;

    /* Wait till previous write/erase is done. */
    if (at45_wait_ready(1)) {
        /* REVISIT status return?? */
        return -1;
    }

    read_addr = (u32)from;
    sector_addr = read_addr / sector_size;
    read_addr = read_addr % len;
    read_addr = read_addr + (sector_addr << addrLen);
    remain_len = (u32)len;

        physical_read_addr = read_addr;
        data_offset = (physical_read_addr % (sector_size));

        SPI_CONTROLLER_Chip_Select_Low(programmerType);
        *ptr++ = 0xe8;
        *ptr++ = (physical_read_addr >> 16) & 0xff;
        *ptr++ = (physical_read_addr >> 8) & 0xff;
        *ptr++ = physical_read_addr & 0xff;
        *ptr++ = 0xff;
        *ptr++ = 0xff;
        *ptr++ = 0xff;
        *ptr++ = 0xff;

        retval = SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 8, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
        retval = SPI_CONTROLLER_Read_NByte(&buf[0],sector_size,SPI_CONTROLLER_SPEED_SINGLE, programmerType);

        if (retval)
        {
           return 0; //error
        }

        SPI_CONTROLLER_Chip_Select_High(programmerType);
     return 1;
}

int at45_write_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType)
{    u32 physical_read_addr;
     u32 read_addr;
     unsigned char addrLen = 9;
     unsigned int sector_addr = 0;
     int retval;
     uint8_t *ptr = spi_buf.obuf;

     programmerType = progType;

     if (sector_size > 511) addrLen++;

     /* sanity checks */
     if (len == 0) return 0;
     /* Wait till previous write/erase is done. */
     if (at45_wait_ready(1)) {
         /* REVISIT status return?? */
         return -1;
     }

     read_addr = (u32)from;
     sector_addr = read_addr / sector_size;
     read_addr = read_addr % len;
     read_addr = read_addr + (sector_addr << addrLen);

         physical_read_addr = read_addr;

         SPI_CONTROLLER_Chip_Select_Low(programmerType);

         *ptr++ = 0x82;
         *ptr++ = (physical_read_addr >> 16) & 0xff;
         *ptr++ = (physical_read_addr >> 8) & 0xff;
         *ptr++ = physical_read_addr & 0xff;
         memcpy(ptr, buf, sector_size);

         retval = SPI_CONTROLLER_Write_NByte(spi_buf.obuf, 4 + sector_size, SPI_CONTROLLER_SPEED_SINGLE, programmerType);
         if (retval)
         {
            return 0; //error
         }

         SPI_CONTROLLER_Chip_Select_High(programmerType);

      return 1;
 }

int at45_full_erase(u8 progType)
{
    programmerType = progType;

    /* Wait until finished previous write command. */
    if (s95_wait_ready(3))
        return -1;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0xC7, programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x94, programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x80, programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x9A, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);

    at45_wait_ready(950);
    return 0;

}


int at45_sector_erase(unsigned int sectorNumber,  unsigned int pageSize, u8 progType)
{
    u32 send_addr = 0;
    unsigned char fullLen = 0x0c;

    programmerType = progType;

    if (pageSize > 511) fullLen++;
    send_addr = sectorNumber << fullLen;
    /* Wait until finished previous write command. */
    if (at45_wait_ready(1))
        return -1;

    SPI_CONTROLLER_Chip_Select_Low(programmerType);
    SPI_CONTROLLER_Write_One_Byte(0x50, programmerType); //block erase command
    SPI_CONTROLLER_Write_One_Byte((send_addr >> 16) & 0xff, programmerType);
    SPI_CONTROLLER_Write_One_Byte((send_addr >> 8) & 0xff, programmerType);
    SPI_CONTROLLER_Write_One_Byte(send_addr & 0xff, programmerType);
    SPI_CONTROLLER_Chip_Select_High(programmerType);

    at45_wait_ready(1);
    return 0;

}

/* NAND APIs return zero on success; negative transport errors are preserved,
 * and positive controller failure codes become NAND_ERROR_TRANSPORT.
 * Unlike the older byte-at-a-time paths, each transaction always attempts to
 * deassert CS, even when asserting CS or sending its command failed. */
#define NAND_CFG_ECC_ENABLE 0x10
#define NAND_CFG_BUFFER_MODE 0x08
#define NAND_SR_BUSY 0x01
#define NAND_SR_ERASE_FAIL 0x04
#define NAND_SR_PROGRAM_FAIL 0x08

static int nand_ecc_status_mode = NAND_ECC_STATUS_NONE;
static int nand_ecc_configuration_valid = 0;
static int nand_ecc_enabled = 0;
static u8 nand_ecc_programmer;
static unsigned int nand_ecc_read_status = NAND_ECC_READ_CLEAN;

static int nand_transfer(u8 *command, u32 command_size, u8 *tx, u32 tx_size,
                         u8 *rx, u32 rx_size)
{
    int ret = SPI_CONTROLLER_Chip_Select_Low(programmerType);
    int cleanup;
    if (!ret)
        ret = SPI_CONTROLLER_Write_NByte(command, command_size,
                                         SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    if (!ret && tx_size)
        ret = SPI_CONTROLLER_Write_NByte(tx, tx_size,
                                         SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    if (!ret && rx_size)
        ret = SPI_CONTROLLER_Read_NByte(rx, rx_size,
                                        SPI_CONTROLLER_SPEED_SINGLE, programmerType);
    cleanup = SPI_CONTROLLER_Chip_Select_High(programmerType);
    ret = ret ? ret : cleanup;
    return ret > 0 ? NAND_ERROR_TRANSPORT : ret;
}

static int nand_read_feature(u8 address, u8 *value)
{
    u8 command[] = {0x0f, address};
    u8 result;
    int ret = nand_transfer(command, sizeof(command), NULL, 0, &result, 1);
    if (!ret) *value = result;
    return ret;
}

static int nand_write_enable_checked(void)
{
    u8 command = OPCODE_WREN;
    int ret = nand_transfer(&command, 1, NULL, 0, NULL, 0);
    if (!ret) imsprog_usleep(1);
    return ret;
}

static int nand_write_feature(u8 address, u8 value)
{
    u8 command[] = {0x1f, address, value};
    int ret = nand_write_enable_checked();
    if (ret) return ret;
    ret = nand_transfer(command, sizeof(command), NULL, 0, NULL, 0);
    if (!ret) imsprog_usleep(2);
    return ret;
}

static int nand_wait_ready_status(int timeout_ms, u8 *status)
{
    uint64_t count, polls;
    u8 sr;
    int ret;
    if (timeout_ms < 0) return NAND_ERROR_INVALID_ARGUMENT;
    /* Poll every 500 microseconds. Include a final sample at the deadline.
     * The old loop multiplied milliseconds by 1000, waiting 500x too long. */
    polls = (uint64_t)timeout_ms * 2 + 1;
    for (count = 0; count < polls; ++count) {
        ret = nand_read_feature(0xc0, &sr);
        if (ret) return ret;
        if (!(sr & NAND_SR_BUSY)) {
            if (status) *status = sr;
            return 0;
        }
        if (count + 1 < polls) imsprog_usleep(500);
    }
    return NAND_ERROR_TIMEOUT;
}

int nand_wait_ready(int sleep_ms)
{
    return nand_wait_ready_status(sleep_ms, NULL);
}

void nand_set_ecc_status_mode(int mode)
{
    /* Device identification belongs to the caller. Do not apply this vendor's
     * ECC encoding to other NANDs, which can have different status layouts. */
    nand_ecc_status_mode = mode == NAND_ECC_STATUS_W25N01GW
                         ? mode : NAND_ECC_STATUS_NONE;
    nand_ecc_configuration_valid = 0;
    nand_reset_ecc_read_status();
}

void nand_reset_ecc_read_status(void)
{
    nand_ecc_read_status = NAND_ECC_READ_CLEAN;
}

unsigned int nand_get_ecc_read_status(void)
{
    return nand_ecc_read_status;
}

int nand_configure_ecc(u8 progType, int enable, int require_buffer_mode, u8 *readback)
{
    u8 before, desired, after;
    int ret;
    programmerType = progType;
    nand_ecc_configuration_valid = 0;
    ret = nand_wait_ready(100);
    if (ret) return ret;
    ret = nand_read_feature(0xb0, &before);
    if (ret) return ret;
    if (readback) *readback = before;
    desired = enable ? (before | NAND_CFG_ECC_ENABLE)
                     : (before & (u8)~NAND_CFG_ECC_ENABLE);
    if (require_buffer_mode) desired |= NAND_CFG_BUFFER_MODE;
    if (desired != before) {
        ret = nand_write_feature(0xb0, desired);
        if (ret) return ret;
        ret = nand_wait_ready(100);
        if (ret) return ret;
    }
    ret = nand_read_feature(0xb0, &after);
    if (ret) return ret;
    if (readback) *readback = after;
    /* Check the entire register, including every preserved non-target bit. */
    if (after != desired) return NAND_ERROR_CONFIG_VERIFY;
    nand_ecc_enabled = (after & NAND_CFG_ECC_ENABLE) != 0;
    nand_ecc_programmer = progType;
    nand_ecc_configuration_valid = 1;
    return 0;
}

int nand_page_read(unsigned char *buf, unsigned int page_size, uint32_t sector_number)
{
    u8 command[] = {0x13, (u8)(sector_number >> 16),
                   (u8)(sector_number >> 8), (u8)sector_number};
    u8 sr;
    int ret;
    if (!buf || !page_size || sector_number > 0xffffffu)
        return NAND_ERROR_INVALID_ARGUMENT;
    ret = nand_wait_ready(100);
    if (ret) return ret;
    ret = nand_transfer(command, sizeof(command), NULL, 0, NULL, 0);
    if (ret) return ret;
    ret = nand_wait_ready_status(100, &sr);
    if (ret) return ret;
    if (nand_ecc_status_mode == NAND_ECC_STATUS_W25N01GW &&
        nand_ecc_configuration_valid && nand_ecc_programmer == programmerType &&
        nand_ecc_enabled) {
        /* W25N01GW datasheet, sections 7.3.2 and 8.2.14: in BUF mode
         * 00 = clean, 01 = corrected, 10 = uncorrectable. Treat unexpected
         * 11 conservatively as uncorrectable too, never as clean data. */
        if ((sr & 0x30) >= 0x20) {
            nand_ecc_read_status |= NAND_ECC_READ_UNCORRECTABLE;
            return NAND_ERROR_ECC_UNCORRECTABLE;
        }
        if ((sr & 0x30) == 0x10)
            nand_ecc_read_status |= NAND_ECC_READ_CORRECTED;
    }
    command[0] = 0x03;
    command[1] = command[2] = command[3] = 0;
    return nand_transfer(command, sizeof(command), NULL, 0, buf, page_size);
}

int nand_read_devid(u8 *rxbuf, int n_rx, u8 progType)
{
    u8 command[] = {OPCODE_RDID, 0x00};
    programmerType = progType;
    nand_ecc_configuration_valid = 0;
    if (!rxbuf || n_rx <= 0) return NAND_ERROR_INVALID_ARGUMENT;
    return nand_transfer(command, sizeof(command), NULL, 0, rxbuf, (u32)n_rx);
}

void nand_write_enable(void)
{
    /* Compatibility entry point. Core operations use the checked helper. */
    (void)nand_write_enable_checked();
}

int nand_unprotect(u8 progType)
{
    u8 before, desired, after;
    int ret;
    programmerType = progType;
    ret = nand_wait_ready(100);
    if (ret) return ret;
    ret = nand_read_feature(0xa0, &before);
    if (ret) return ret;
    desired = before & 0x83; /* Preserve WP-E/SRP; clear only BP/TB bits. */
    if (desired != before) {
        ret = nand_write_feature(0xa0, desired);
        if (ret) return ret;
        ret = nand_wait_ready(100);
        if (ret) return ret;
    }
    ret = nand_read_feature(0xa0, &after);
    if (ret) return ret;
    return after == desired ? 0 : NAND_ERROR_CONFIG_VERIFY;
}

int nand_block_erase(unsigned int sector_number, unsigned int blockSize, u8 progType)
{
    u8 command[4], sr;
    int ret;
    (void)blockSize; /* Existing NAND geometry uses 64 pages per erase block. */
    programmerType = progType;
    if (sector_number > (0xffffffu >> 6)) return NAND_ERROR_INVALID_ARGUMENT;
    ret = nand_wait_ready(3000);
    if (ret) return ret;
    ret = nand_unprotect(progType);
    if (ret) return ret;
    /* SET FEATURE can clear WEL on some devices; set it after unprotect. */
    ret = nand_write_enable_checked();
    if (ret) return ret;
    sector_number <<= 6;
    command[0] = 0xd8;
    command[1] = (u8)(sector_number >> 16);
    command[2] = (u8)(sector_number >> 8);
    command[3] = (u8)sector_number;
    ret = nand_transfer(command, sizeof(command), NULL, 0, NULL, 0);
    if (ret) return ret;
    ret = nand_wait_ready_status(3000, &sr);
    if (ret) return ret;
    return (sr & NAND_SR_ERASE_FAIL) ? NAND_ERROR_ERASE_FAIL : 0;
}

int nand_page_write(unsigned char *buf, unsigned int page_size, uint32_t sector_number)
{
    u8 command[] = {0x02, 0, 0, 0};
    u8 sr;
    int ret;
    if (!buf || !page_size || sector_number > 0xffffffu)
        return NAND_ERROR_INVALID_ARGUMENT;
    ret = nand_wait_ready(950);
    if (ret) return ret;
    ret = nand_write_enable_checked();
    if (ret) return ret;
    ret = nand_transfer(command, 3, buf, page_size, NULL, 0);
    if (ret) return ret;
    ret = nand_wait_ready(200);
    if (ret) return ret;
    command[0] = 0x10;
    command[1] = (u8)(sector_number >> 16);
    command[2] = (u8)(sector_number >> 8);
    command[3] = (u8)sector_number;
    ret = nand_transfer(command, sizeof(command), NULL, 0, NULL, 0);
    if (ret) return ret;
    /* Wait for this page, including the final page, and inspect P-FAIL. */
    ret = nand_wait_ready_status(950, &sr);
    if (ret) return ret;
    return (sr & NAND_SR_PROGRAM_FAIL) ? NAND_ERROR_PROGRAM_FAIL : 0;
}

static int nand_block_arguments_valid(unsigned char *buf, unsigned int page_size,
                                      u32 block, unsigned int pages)
{
    if (!buf || !page_size || !pages) return 0;
    if ((uint64_t)block * pages + pages - 1 > 0xffffffu) return 0;
    return (uint64_t)page_size * pages <= SIZE_MAX;
}

int nand_block_read(unsigned char *buf, unsigned int page_size, uint32_t block_number,
                    unsigned int pages_per_block, u8 progType)
{
    unsigned int i;
    int ret;
    programmerType = progType;
    if (!nand_block_arguments_valid(buf, page_size, block_number, pages_per_block))
        return NAND_ERROR_INVALID_ARGUMENT;
    for (i = 0; i < pages_per_block; ++i) {
        ret = nand_page_read(buf + (size_t)i * page_size, page_size,
                             block_number * pages_per_block + i);
        if (ret) return ret;
    }
    return 0;
}

int nand_block_write(unsigned char *buf, unsigned int page_size, uint32_t block_number,
                     unsigned int pages_per_block, u8 progType)
{
    unsigned int i;
    int ret;
    programmerType = progType;
    if (!nand_block_arguments_valid(buf, page_size, block_number, pages_per_block))
        return NAND_ERROR_INVALID_ARGUMENT;
    for (i = 0; i < pages_per_block; ++i) {
        ret = nand_page_write(buf + (size_t)i * page_size, page_size,
                              block_number * pages_per_block + i);
        if (ret) return ret;
    }
    return 0;
}

void nand_ECCEnable(u8 progType)
{
    /* Legacy callers must migrate to the checked configuration API. */
    (void)nand_configure_ecc(progType, 1, 0, NULL);
}

void nand_ECCDisable(u8 progType)
{
    (void)nand_configure_ecc(progType, 0, 0, NULL);
}

int nand_checkBadBlock(uint32_t blockNo, uint32_t sectSize,
                       uint32_t blockPerSector, u8 progType)
{
    u8 command[4], buf[2];
    uint64_t page = (uint64_t)blockNo * blockPerSector;
    int ret;
    programmerType = progType;
    /* Preserve this legacy API's 0=good, 1=bad, -1=error convention. */
    if (!blockPerSector || !sectSize || sectSize > 0xfffeu || page > 0xffffffu)
        return -1;
    ret = nand_wait_ready(950);
    if (ret) return -1;
    command[0] = 0x13;
    command[1] = (u8)(page >> 16);
    command[2] = (u8)(page >> 8);
    command[3] = (u8)page;
    ret = nand_transfer(command, sizeof(command), NULL, 0, NULL, 0);
    if (ret) return -1;
    ret = nand_wait_ready(950);
    if (ret) return -1;
    command[0] = 0x03;
    command[1] = (u8)(sectSize >> 8);
    command[2] = (u8)sectSize;
    command[3] = 0;
    ret = nand_transfer(command, sizeof(command), NULL, 0, buf, sizeof(buf));
    if (ret) return -1;
    return buf[0] == 0xff ? 0 : 1;
}
