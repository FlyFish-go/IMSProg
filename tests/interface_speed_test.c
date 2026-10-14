#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ch341a_spi.h"
#include "ch347.h"
#include "ft232all.h"
#include "spi_controller.h"

struct ch347_priv *priv;

int ch341a_init(uint8_t chip_type, uint16_t speed) { return 0; }
int ch341a_spi_shutdown(void) { return 0; }
int ch341a_spi_send_command(unsigned int write_count, unsigned int read_count,
                            const unsigned char *write_data, unsigned char *read_data) { return 0; }
int enable_pins(bool enable) { return 0; }
int ch341GetDescriptor(uint8_t *buffer) { return 0; }
void ch347_select_model(int model) {}
bool ch347_spi_init(uint8_t chip_type, uint16_t speed) { return false; }
void ch347_spi_shutdown(void) {}
int ch347GetDescriptor(uint8_t *buffer) { return 0; }
int ch347_spi_tx(struct ch347_priv *state, const void *data, uint32_t length) { return 0; }
int ch347_spi_rx(struct ch347_priv *state, void *data, uint32_t length) { return 0; }
int ch347_set_cs(struct ch347_priv *state, int chip_select, int value) { return 0; }
int initFt232h(void) { return 0; }
void closeFt232h(void) {}
int ft232hGetDescriptor(uint8_t *buffer) { return 0; }
int ft232hSetSpeedSPI(uint16_t speed) { return 0; }
int ft232hSetSpeedI2C(uint16_t speed) { return 0; }
int ft232h_CS_LO(void) { return 0; }
int ft232h_CS_HI(void) { return 0; }
int ft232WriteNbytes(uint8_t *buffer, uint32_t size) { return 0; }
int ft232ReadNbytes(uint8_t *buffer, uint32_t size) { return 0; }

static void expect_speed(const char *name, uint16_t actual, uint16_t expected)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: got %u, expected %u\n",
                name, (unsigned int)actual, (unsigned int)expected);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    /* Existing programmers keep the original auxiliary-dialog limits. */
    expect_speed("CH341 NAND cap", getAuxiliaryInterfaceSpeed(0, 6, 15000), 15000);
    expect_speed("CH347T NAND cap", getAuxiliaryInterfaceSpeed(2, 6, 15000), 15000);
    expect_speed("CH347T EEPROM cap", getAuxiliaryInterfaceSpeed(3, 3, 5000), 5000);
    expect_speed("FT232H NOR cap", getAuxiliaryInterfaceSpeed(4, 0, 5000), 5000);

    /* CH347F presets may lower, but never raise, an auxiliary-dialog limit. */
    expect_speed("CH347F standard NAND", getAuxiliaryInterfaceSpeed(5, 6, 15000), 15000);
    expect_speed("CH347F high NAND", getAuxiliaryInterfaceSpeed(6, 6, 15000), 15000);
    expect_speed("CH347F low NAND", getAuxiliaryInterfaceSpeed(7, 6, 15000), 7500);
    expect_speed("CH347F standard EEPROM", getAuxiliaryInterfaceSpeed(5, 3, 5000), 5000);
    expect_speed("CH347F high EEPROM", getAuxiliaryInterfaceSpeed(6, 3, 5000), 5000);
    expect_speed("CH347F low EEPROM", getAuxiliaryInterfaceSpeed(7, 3, 5000), 1875);
    expect_speed("CH347F low NOR fixed cap", getAuxiliaryInterfaceSpeed(7, 0, 5000), 5000);
    expect_speed("CH347F low 45xx", getAuxiliaryInterfaceSpeed(7, 5, 5000), 1875);

    puts("PASS: CH347F auxiliary speed limits");
    return EXIT_SUCCESS;
}
