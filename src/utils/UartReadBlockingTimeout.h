#ifndef SMART_LCC_UARTREADBLOCKINGTIMEOUT_H
#define SMART_LCC_UARTREADBLOCKINGTIMEOUT_H

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static inline bool uart_read_blocking_timeout(uart_inst_t *uart, uint8_t *dst, size_t len, absolute_time_t timeout_time) {
    for (size_t i = 0; i < len; ++i) {
        while (!uart_is_readable(uart)) {
            if (time_reached(timeout_time)) {
                return false;
            }
            tight_loop_contents();
        }
        *dst++ = uart_get_hw(uart)->dr;
    }
    return true;
}

#endif //SMART_LCC_UARTREADBLOCKINGTIMEOUT_H
