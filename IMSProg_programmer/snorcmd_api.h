/*
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * 2023-2024 Mikhail Medvedev <e-ink-reader@yandex.ru>
 * snorcmd_api.h
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
#ifndef __SNORCMD_API_H__
#define __SNORCMD_API_H__
#include "types.h"

int snor_wait_ready(int sleep_ms);
int snor_read(unsigned char *buf, unsigned long from, unsigned long len);
int snor_read_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned int addr4b, u8 progType);
int snor_write_param(unsigned char *buf, unsigned long to, unsigned long len, unsigned int sector_size, unsigned int addr4b, u8 progType);
int snorUnprotect(u8 progType);
int snor_erase(unsigned long offs, unsigned long len);
int full_erase_chip(void);
int snor_block_erase(unsigned int sector_number, unsigned int blockSize, u8 addr4b, u8 progType);
int snor_write(unsigned char *buf, unsigned long to, unsigned long len);
long snor_init(void);
void support_snor_list(void);
int snor_read_devid(u8 *rxbuf, int n_rx, u8 progType);
int s95_read_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType);
int s95_write_param(unsigned char *buf, unsigned long to, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType);
int s95_unprotect(void);
int s95_full_erase(u8 progType);
int at45_read_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType);
int at45_write_param(unsigned char *buf, unsigned long from, unsigned long len, unsigned int sector_size, unsigned char currentAlgorithm, u8 progType);
int at45_full_erase(u8 progType);
int at45_sector_erase(unsigned int sectorNumber, unsigned int pageSize, u8 progType);
int at45_read_sr(u8 *val);
/* NAND operations return 0 on success, otherwise a negative transport error or one
 * of these errors (positive controller failures become NAND_ERROR_TRANSPORT).
 * Exception: nand_checkBadBlock returns 0/1/-1. */
#define NAND_ERROR_CONFIG_VERIFY (-1001)
#define NAND_ERROR_ECC_UNCORRECTABLE (-1002)
#define NAND_ERROR_PROGRAM_FAIL (-1003)
#define NAND_ERROR_ERASE_FAIL (-1004)
#define NAND_ERROR_TIMEOUT (-1005)
#define NAND_ERROR_INVALID_ARGUMENT (-1006)
#define NAND_ERROR_TRANSPORT (-1007)
#define NAND_ECC_STATUS_NONE 0
#define NAND_ECC_STATUS_WINBOND 1   /* SR[5:4], 1x = uncorrectable; needs BUF=1 */
#define NAND_ECC_STATUS_W25N01GW NAND_ECC_STATUS_WINBOND
#define NAND_ECC_STATUS_MICRON 2    /* SR[6:4], 010 = uncorrectable */
#define NAND_ECC_STATUS_GENERIC 3   /* SR[5:4], 10 = uncorrectable */
#define NAND_ECC_MAX_RECORDED_PAGES 4096u
#define NAND_ECC_READ_CLEAN 0u
#define NAND_ECC_READ_CORRECTED 1u
#define NAND_ECC_READ_UNCORRECTABLE 2u
/* Set mode only after identifying the physical device. This invalidates cached
 * ECC configuration; call nand_configure_ecc afterwards for each operation.
 * These APIs share the existing single-programmer global state; not thread-safe. */
void nand_set_ecc_status_mode(int mode);
/* Pick the ECC status decoding for a JEDEC manufacturer ID. */
int nand_ecc_status_mode_for_id(u8 manufacturer_id);
unsigned int nand_decode_ecc_status(int mode, u8 status_register);
/* Reset before a whole read/verify, not between blocks. Get returns ORed flags.
 * With on-die ECC enabled, nand_page_read never fails on an uncorrectable
 * page: it reads the page anyway and records it here. */
void nand_reset_ecc_read_status(void);
unsigned int nand_get_ecc_read_status(void);
unsigned int nand_get_ecc_corrected_pages(void);
unsigned int nand_get_ecc_uncorrectable_pages(void);
/* Copies up to max recorded page numbers (first NAND_ECC_MAX_RECORDED_PAGES). */
unsigned int nand_get_ecc_uncorrectable_page_list(u32 *pages, unsigned int max);
/* Change B0 bit 4 only; if required for a known Winbond device also set BUF bit 3.
 * Preserve and verify all other bits. readback may be NULL; on mismatch it holds
 * the actual B0 value. Never call this implicitly from a read or verify helper. */
int nand_configure_ecc(u8 progType, int enable, int require_buffer_mode, u8 *readback);
int nand_page_read(unsigned char *buf, unsigned int sector_size, u32 sector_number);
int nand_block_read(unsigned char *buf, unsigned int page_size, u32 block_number, unsigned int pages_per_block, u8 progType);
int nand_wait_ready(int sleep_ms);
int nand_read_devid(u8 *rxbuf, int n_rx, u8 progType);
int nand_block_erase(unsigned int block_number, unsigned int pages_per_block, u8 progType);
void nand_write_enable(void);
int nand_unprotect(u8 progType);
int nand_page_write(unsigned char *buf, unsigned int page_size, u32 sector_number);
int nand_block_write(unsigned char *buf, unsigned int page_size, u32 block_number, unsigned int pages_per_block, u8 progType);
void nand_ECCEnable(u8 progType);
void nand_ECCDisable(u8 progType);
int nand_checkBadBlock(u32 blockNo, u32 sectSize, u32 blockPerSector, u8 progType);
#endif /* __SNORCMD_API_H__ */
/* End of [snorcmd_api.h] package */
