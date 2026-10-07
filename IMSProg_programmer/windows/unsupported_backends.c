/* SPDX-License-Identifier: GPL-3.0-or-later
 * Deliberately unsupported backends for the CH341A/B and CH347T SPI-only Windows build.
 * Every operation fails closed. The UI must disable these devices/protocols.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ch341a_spi.h"
#include "ch341a_gpio.h"
#include "ch34x_i2c.h"
#include "ft232all.h"
#include "bitbang_microwire.h"
#include "mw_eeprom_api.h"

#define UNSUPPORTED (-1)
int ch341a_gpio_setdir(void) { return UNSUPPORTED; }
int ch341a_gpio_setbits(uint8_t bits) { (void)bits; return UNSUPPORTED; }
int ch341a_gpio_getbits(uint8_t *data) { (void)data; return UNSUPPORTED; }
int ch34xi2cBlockRead(uint8_t *buf, uint32_t address, uint32_t size, uint8_t algorithm, uint8_t device)
{ (void)buf; (void)address; (void)size; (void)algorithm; (void)device; return UNSUPPORTED; }
int ch34xi2cBlockWrite(uint8_t *buf, uint32_t address, uint32_t size, uint32_t sector, uint8_t algorithm, uint8_t device)
{ (void)buf; (void)address; (void)size; (void)sector; (void)algorithm; (void)device; return UNSUPPORTED; }
int initFt232h(void) { return UNSUPPORTED; }
void closeFt232h(void) {}
int ft232hGetDescriptor(uint8_t *buf) { if (buf) memset(buf, 0, 18); return UNSUPPORTED; }
int ft232hSetSpeedSPI(uint16_t speed) { (void)speed; return UNSUPPORTED; }
int ft232h_CS_LO(void) { return UNSUPPORTED; }
int ft232h_CS_HI(void) { return UNSUPPORTED; }
int ft232WriteNbytes(uint8_t *buf, uint32_t size) { (void)buf; (void)size; return UNSUPPORTED; }
int ft232ReadNbytes(uint8_t *buf, uint32_t size) { (void)buf; (void)size; return UNSUPPORTED; }
int ft232hSetSpeedI2C(uint16_t speed) { (void)speed; return UNSUPPORTED; }
int ft232I2cBlockWrite(uint8_t *data, uint32_t address, uint32_t size, uint32_t sector, uint8_t algorithm)
{ (void)data; (void)address; (void)size; (void)sector; (void)algorithm; return UNSUPPORTED; }
int ft232I2cBlockRead(uint8_t *data, uint32_t address, uint32_t size, uint8_t algorithm)
{ (void)data; (void)address; (void)size; (void)algorithm; return UNSUPPORTED; }
int initFt232hMW(void) { return UNSUPPORTED; }
int ft232MWWriteEnable(uint8_t algorithm) { (void)algorithm; return UNSUPPORTED; }
int ft232MWWriteDisable(uint8_t algorithm) { (void)algorithm; return UNSUPPORTED; }
int ft232hMWEraseAll(uint8_t algorithm) { (void)algorithm; return UNSUPPORTED; }
int ft232MWReadBlock(uint8_t *buf, uint16_t start, uint16_t size, uint8_t algorithm)
{ (void)buf; (void)start; (void)size; (void)algorithm; return UNSUPPORTED; }
int ft232MWWriteBlock(uint8_t *buf, uint16_t start, uint16_t size, uint8_t algorithm)
{ (void)buf; (void)start; (void)size; (void)algorithm; return UNSUPPORTED; }
int Read_EEPROM_3wire_param(unsigned char *buf, int start, int size, int total, unsigned char algorithm)
{ (void)buf; (void)start; (void)size; (void)total; (void)algorithm; return UNSUPPORTED; }
int Write_EEPROM_3wire_param(unsigned char *buf, int start, int size, int total, unsigned char algorithm)
{ (void)buf; (void)start; (void)size; (void)total; (void)algorithm; return UNSUPPORTED; }
void Erase_EEPROM_3wire_param(unsigned char algorithm) { (void)algorithm; }
int mw_gpio_init(void) { return UNSUPPORTED; }
