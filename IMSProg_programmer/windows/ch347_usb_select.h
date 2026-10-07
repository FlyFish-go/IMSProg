/* SPDX-License-Identifier: BSD-1-Clause */
#ifndef IMSPROG_CH347_USB_SELECT_H
#define IMSPROG_CH347_USB_SELECT_H
#include "vendor/libusb.h"
int ch34x_usb_select_bulk_interface(const struct libusb_config_descriptor *config,
                                    unsigned char out_endpoint, unsigned char in_endpoint,
                                    int *interface_number, int *alternate_setting);
/* 0: unique SPI bulk interface; -1: absent; -2: ambiguous/malformed. */
int ch347_usb_select_spi_interface(const struct libusb_config_descriptor *config,
                                  int *interface_number, int *alternate_setting);
#endif
