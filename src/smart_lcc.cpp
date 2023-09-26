#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pins.h"
#include <u8g2.h>
#include <cstring>
#include "u8g2functions.h"
#include "Controller/Core0/SystemController.h"
#include "utils/PicoQueue.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "MulticoreSupport.h"
#include "Controller/SingleCore/EspBootloader.h"
#include "utils/ClearUartCruft.h"
#include "utils/UartReadBlockingTimeout.h"
#include "Controller/Core1/UIController.h"
#include "Controller/Core1/EspFirmware.h"
#include "utils/USBDebug.h"
#include "firmware_crc.h"
#include "pico/binary_info.h"

repeating_timer_t safePacketBootupTimer;
u8g2_t u8g2;
SystemController* systemController;
SystemStatus* status;
UIController* uiController;
PicoQueue<SystemControllerStatusMessage>* statusQueue;
PicoQueue<SystemControllerCommand>* commandQueue;
MulticoreSupport support;
EspFirmware *espFirmware;
extern "C" {
volatile bool __otherCoreIdled = false;
};

[[noreturn]] void main1();

void initGpio() {
    bi_decl(bi_2pins_with_func(ESP_RX, ESP_TX, GPIO_FUNC_UART));

    gpio_set_function(ESP_RX, GPIO_FUNC_UART);
    gpio_set_function(ESP_TX, GPIO_FUNC_UART);
    //uart_set_hw_flow(uart0, false, false);
    uart_init(uart0, 115200);

    bi_decl(bi_2pins_with_func(CB_RX, CB_TX, GPIO_FUNC_UART));

    gpio_set_function(CB_RX, GPIO_FUNC_UART);
    gpio_set_inover(CB_RX, GPIO_OVERRIDE_INVERT);
    gpio_set_function(CB_TX, GPIO_FUNC_UART);
    gpio_set_outover(CB_TX, GPIO_OVERRIDE_INVERT);
    uart_init(uart1, 9600);
}

[[noreturn]] void main0() {
    watchdog_enable(2000, true);

    cancel_repeating_timer(&safePacketBootupTimer);
    support.registerCore();

    // Core 0 - System controller (incl. safe packet sender), Settings controller
    nonstd::optional<absolute_time_t> core1RebootTimer{};

    while(true) {
        if (watchdog_get_count() > 0) {
            watchdog_update();
        }

        if (core1RebootTimer.has_value() && absolute_time_diff_us(core1RebootTimer.value(), get_absolute_time()) > 0) {
            multicore_reset_core1();
            multicore_launch_core1(main1);
            core1RebootTimer = make_timeout_time_ms(5000);
        }

        systemController->loop();
//        espFirmware->loop();

        if (statusQueue->isFull())  {
            if (!core1RebootTimer.has_value()) {
                core1RebootTimer = make_timeout_time_ms(2000);
            }
        }
    }
}

[[noreturn]] void main1() {
    u8g2Char('X');
    support.registerCore();
    SystemControllerStatusMessage sm;

    u8g2Char('Y');

    uiController = new UIController(status, commandQueue, &u8g2, MINUS_BUTTON, PLUS_BUTTON);

    u8g2Char('Z');

    SystemControllerCommand beginCommand;
    beginCommand.type = COMMAND_BEGIN;
    commandQueue->tryAdd(&beginCommand);

    absolute_time_t nextSend = make_timeout_time_ms(1000);

    espFirmware = new EspFirmware(uart1, commandQueue, status);
    EspFirmware::initInterrupts(uart1);

    //gpio_put(AUX_TX, true);

    while (true) {
        while (!statusQueue->isEmpty()) {
            statusQueue->removeBlocking(&sm);
        }

        status->updateStatusMessage(sm);
        uiController->loop();
        espFirmware->loop();

        if (time_reached(nextSend)) {
            espFirmware->sendStatus(&sm);
            nextSend = make_timeout_time_ms(250);
        }
    }

    // Core 1 - UI Controller, ESP UART controller

}

bool repeating_timer_callback([[maybe_unused]] repeating_timer_t *t) {
    systemController->sendSafePacketNoWait();
    return true;
}

int main() {
    stdio_usb_init();

    initGpio();
    initU8g2();
    u8g2Char('A');

    support.begin(2);

    u8g2Char('B');

    statusQueue = new PicoQueue<SystemControllerStatusMessage>(100);
    commandQueue = new PicoQueue<SystemControllerCommand>(100);

    // Move inside SystemController
    SystemSettings settings(commandQueue, &support);
    settings.initialize();
    // End move

    u8g2Char('C');

    printf("Test\n");

    systemController = new SystemController(uart0, statusQueue, commandQueue, &settings);
    add_repeating_timer_ms(1000, repeating_timer_callback, NULL, &safePacketBootupTimer);

    u8g2Char('D');

    initEsp();

    u8g2Char('E');

    status = new SystemStatus();

    multicore_reset_core1();
    multicore_launch_core1(main1);

    main0();
}
