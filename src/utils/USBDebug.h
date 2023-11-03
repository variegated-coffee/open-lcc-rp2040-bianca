//
// Created by Magnus Nordlander on 2023-01-07.
//

#ifndef SMART_LCC_USBDEBUG_H
#define SMART_LCC_USBDEBUG_H

#include <cstdio>

#ifdef USB_DEBUG
#define INIT_USB_DEBUG() stdio_usb_init()
#define USB_PRINTF(...) printf (__VA_ARGS__)
#define USB_DEBUG_DELAY() sleep_ms(5000)
#else
#define INIT_USB_DEBUG() do {} while (0)
#define USB_PRINTF(...) do {} while (0)
#define USB_DEBUG_DELAY() do {} while (0)
#endif

#define USB_PRINT_BUF(BUF, LEN) for (unsigned int __i = 0; __i < LEN; __i++) { USB_PRINTF("%02X", ((uint8_t*)BUF)[__i]); }

#endif //SMART_LCC_USBDEBUG_H
