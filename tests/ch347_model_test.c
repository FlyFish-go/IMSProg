/* Hardware-free checks: CH347T/CH347F selection opens the right USB PID and interface. */
#include "ch347.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t opened_pid;
static int claimed_if = -1, released_if = -1, attached_pid = 0;
static struct libusb_device_handle *fake_handle = (struct libusb_device_handle *)&opened_pid;

int LIBUSB_CALL libusb_init(libusb_context **ctx) { *ctx = (libusb_context *)&claimed_if; return 0; }
void LIBUSB_CALL libusb_exit(libusb_context *ctx) { (void)ctx; }
int LIBUSB_CALLV libusb_set_option(libusb_context *ctx, enum libusb_option option, ...) { (void)ctx; (void)option; return 0; }
libusb_device_handle * LIBUSB_CALL libusb_open_device_with_vid_pid(libusb_context *ctx, uint16_t vid, uint16_t pid)
{
    (void)ctx;
    opened_pid = pid;
    return (vid == CH347_SPI_VID && pid == attached_pid) ? fake_handle : NULL;
}
int LIBUSB_CALL libusb_set_auto_detach_kernel_driver(libusb_device_handle *h, int enable) { (void)h; (void)enable; return 0; }
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *h, int iface) { assert(h == fake_handle); claimed_if = iface; return 0; }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *h, int iface) { assert(h == fake_handle); released_if = iface; return 0; }
void LIBUSB_CALL libusb_close(libusb_device_handle *h) { assert(h == fake_handle); }
const char * LIBUSB_CALL libusb_error_name(int code) { (void)code; return "fake"; }
int LIBUSB_CALL libusb_control_transfer(libusb_device_handle *h, uint8_t a, uint8_t b, uint16_t c, uint16_t d,
                                        unsigned char *e, uint16_t f, unsigned int g)
{ (void)h; (void)a; (void)b; (void)c; (void)d; (void)e; (void)g; return f; }
static uint8_t last_cmd;
int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep, unsigned char *data, int length,
                                     int *transferred, unsigned int timeout)
{
    (void)h; (void)timeout;
    if (ep == CH347_EPOUT) { last_cmd = data[0]; *transferred = length; return 0; }
    /* INFO_RD returns the 26-byte config; other commands return a 1-byte ACK. */
    int payload = last_cmd == CH347_CMD_INFO_RD ? (int)sizeof(struct ch347_spi_hw_config) : 1;
    assert(length >= payload + 3);
    memset(data, 0, (size_t)(payload + 3));
    data[0] = last_cmd; data[1] = (uint8_t)payload; data[2] = 0;
    *transferred = payload + 3;
    return 0;
}

int main(void)
{
    struct ch347_priv *p;
    assert(ch347_model_pid(CH347_MODEL_T) == 0x55db && ch347_model_interface(CH347_MODEL_T) == 2);
    assert(ch347_model_pid(CH347_MODEL_F) == 0x55de && ch347_model_interface(CH347_MODEL_F) == 4);
    assert(!strcmp(ch347_model_name(CH347_MODEL_F), "CH347F"));
    assert(ch347_model_wch_chip_type(CH347_MODEL_F) == 2 && ch347_model_wch_func_type(CH347_MODEL_F) == 3);
    assert(ch347_model_wch_chip_mode(CH347_MODEL_F) == 0);
    assert(ch347_selected_model() == CH347_MODEL_T); /* default keeps existing CH347T behaviour */

    attached_pid = 0x55de;
    ch347_select_model(CH347_MODEL_F);
    p = ch347_open();
    assert(p && opened_pid == 0x55de && claimed_if == 4 && p->model == CH347_MODEL_F);
    ch347_close(p);
    assert(released_if == 4);
    /* Full SPI init path (NOR, 30 MHz) on CH347F. */
    assert(ch347_spi_init(0, 30000) == 0);
    ch347_spi_shutdown();

    /* CH347T selected while only a CH347F is attached: must not open it. */
    ch347_select_model(CH347_MODEL_T);
    claimed_if = -1;
    assert(ch347_open() == NULL && opened_pid == 0x55db && claimed_if == -1);

    attached_pid = 0x55db;
    p = ch347_open();
    assert(p && claimed_if == 2 && p->model == CH347_MODEL_T);
    ch347_close(p);
    assert(released_if == 2);

    ch347_select_model(42); /* invalid -> CH347T */
    assert(ch347_selected_model() == CH347_MODEL_T);
    puts("CH347 model selection: CH347T 1A86:55DB/IF2 and CH347F 1A86:55DE/IF4 open, claim, release and SPI init passed");
    return 0;
}
