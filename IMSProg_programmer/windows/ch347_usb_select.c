/* SPDX-License-Identifier: BSD-1-Clause */
#include "ch347_usb_select.h"

int ch34x_usb_select_bulk_interface(const struct libusb_config_descriptor *config,
                                    unsigned char out_endpoint, unsigned char in_endpoint,
                                    int *interface_number, int *alternate_setting)
{
    int matches = 0;
    if (!config || !interface_number || !alternate_setting ||
        (config->bNumInterfaces && !config->interface)) return -1;
    *interface_number = *alternate_setting = -1;
    for (unsigned i = 0; i < config->bNumInterfaces; ++i) {
        const struct libusb_interface *iface = &config->interface[i];
        if (iface->num_altsetting < 0 || (iface->num_altsetting && !iface->altsetting)) return -2;
        for (int a = 0; a < iface->num_altsetting; ++a) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
            int in = 0, out = 0;
            if (alt->bNumEndpoints && !alt->endpoint) return -2;
            for (unsigned e = 0; e < alt->bNumEndpoints; ++e) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                if (ep->bEndpointAddress == in_endpoint) ++in;
                if (ep->bEndpointAddress == out_endpoint) ++out;
            }
            if (in > 1 || out > 1) return -2;
            if (in == 1 && out == 1) {
                ++matches;
                *interface_number = alt->bInterfaceNumber;
                *alternate_setting = alt->bAlternateSetting;
            }
        }
    }
    return matches == 1 ? 0 : (matches ? -2 : -1);
}

int ch347_usb_select_spi_interface(const struct libusb_config_descriptor *config,
                                  int *interface_number, int *alternate_setting)
{
    return ch34x_usb_select_bulk_interface(config, 0x06, 0x86, interface_number, alternate_setting);
}
