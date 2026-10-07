/* Hardware-free tests of descriptor-driven SPI interface selection. */
#include "windows/ch347_usb_select.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    struct libusb_endpoint_descriptor endpoints[3] = {{0}};
    struct libusb_interface_descriptor alternates[2] = {{0}};
    struct libusb_interface interfaces[3] = {{0}};
    struct libusb_config_descriptor config = {0};
    int iface, alt;
    endpoints[0].bEndpointAddress = 0x86;
    endpoints[1].bEndpointAddress = 0x06;
    endpoints[0].bmAttributes = endpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
    alternates[0].bNumEndpoints = 2;
    alternates[0].endpoint = endpoints;
    interfaces[0].num_altsetting = 1;
    interfaces[0].altsetting = alternates;
    config.bNumInterfaces = 1;
    config.interface = interfaces;
    for (int i = 0; i < 16; ++i) {
        alternates[0].bInterfaceNumber = (uint8_t)i;
        for (int a = 0; a < 4; ++a) {
            alternates[0].bAlternateSetting = (uint8_t)a;
            assert(ch347_usb_select_spi_interface(&config, &iface, &alt) == 0);
            assert(iface == i && alt == a);
        }
    }
    endpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_INTERRUPT;
    assert(ch347_usb_select_spi_interface(&config, &iface, &alt) == -1);
    endpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
    endpoints[1].bEndpointAddress = 0x02;
    assert(ch347_usb_select_spi_interface(&config, &iface, &alt) == -1);
    endpoints[1].bEndpointAddress = 0x06;
    alternates[1] = alternates[0];
    interfaces[0].num_altsetting = 2;
    assert(ch347_usb_select_spi_interface(&config, &iface, &alt) == -2);
    interfaces[0].num_altsetting = 1;
    interfaces[1] = interfaces[0];
    config.bNumInterfaces = 2;
    assert(ch347_usb_select_spi_interface(&config, &iface, &alt) == -2);
    config.bNumInterfaces = 1;
    endpoints[2] = endpoints[0];
    alternates[0].bNumEndpoints = 3;
    assert(ch347_usb_select_spi_interface(&config, &iface, &alt) == -2);
    alternates[0].endpoint = NULL;
    assert(ch347_usb_select_spi_interface(&config, &iface, &alt) == -2);
    assert(ch347_usb_select_spi_interface(NULL, &iface, &alt) == -1);
    puts("Descriptor selection: 64 valid interface/alternate pairs and absent/ambiguous/malformed cases passed");
}
