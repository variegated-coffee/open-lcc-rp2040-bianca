#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/i2c.h"
#include <cstring>
#include "Controller/Core0/SystemController.h"
#include "utils/PicoQueue.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "MulticoreSupport.h"
#include "utils/UartReadBlockingTimeout.h"
#include "Controller/Core1/EspFirmware.h"
#include "Controller/Core1/MCP9600.h"
#include "Controller/Core1/SettingsFlash.h"
#include "pico/binary_info.h"
#include "f_util.h"
#include "hw_config.h"
#include "utils/USBDebug.h"
#include "pins.h"
#include "Controller/Core1/Automations.h"
#include "ff.h"  // This should give you access to STA_NOINIT and other SD card status flags

repeating_timer_t safePacketBootupTimer;
SystemController* systemController;
SystemStatus* status;
SettingsFlash* settingsFlash;
SettingsManager* settingsManager;
PicoQueue<SystemControllerStatusMessage>* statusQueue;
PicoQueue<SystemControllerCommand>* commandQueue;
MulticoreSupport support;
EspFirmware *espFirmware;
MCP9600* mcp9600_0x60;
MCP9600* mcp9600_0x67;
MCP9600* mcp9600_0x63;
Automations* automations;

/* SDIO Interface */
static sd_sdio_if_t sdio_if = {
    .CLK_gpio = SD_MISO_DAT0 - 2,  // CLK is offset by -2 from D0
    .CMD_gpio = SD_MOSI_CMD,
    .D0_gpio = SD_MISO_DAT0,
    .D1_gpio = SD_MISO_DAT0 + 1,   // D1 is offset by +1 from D0
    .D2_gpio = SD_MISO_DAT0 + 2,   // D2 is offset by +2 from D0
    .D3_gpio = SD_MISO_DAT0 + 3,   // D3 is offset by +3 from D0
    .SDIO_PIO = pio0,
    .DMA_IRQ_num = DMA_IRQ_0,
    .use_exclusive_DMA_IRQ_handler = false,
    .baud_rate = 15 * 1000 * 1000,  // 15 MHz
    .set_drive_strength = true,
    .CLK_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
    .CMD_gpio_drive_strength = GPIO_DRIVE_STRENGTH_8MA,
    .D0_gpio_drive_strength = GPIO_DRIVE_STRENGTH_8MA,
    .D1_gpio_drive_strength = GPIO_DRIVE_STRENGTH_8MA,
    .D2_gpio_drive_strength = GPIO_DRIVE_STRENGTH_8MA,
    .D3_gpio_drive_strength = GPIO_DRIVE_STRENGTH_8MA,
    .state = {}
};

/* Hardware Configuration of the SD Card socket "object" */
static sd_card_t sd_card = {
    .pcName = "0:",
    .type = SD_IF_SDIO,
    .sdio_if_p = &sdio_if,
    .use_card_detect = true,
    .card_detect_gpio = SD_DET_B,
    .card_detected_true = false,
    .card_detect_use_pull = true,
    .card_detect_pull_hi = true,
    .m_Status = 0,  // Instead of STA_NOINIT which wasn't found
    .csd = {},
    .cid = {},
    .sectors = 0,
    .card_type = 0,  // Instead of SDCARD_NONE
    .mutex = nullptr,
    .fatfs = {},
    .mounted = false,
    .init = nullptr,
    .write_blocks = nullptr,
    .read_blocks = nullptr,
    .get_num_sectors = nullptr,
    .sd_test_com = nullptr
};

/* Callbacks used by the library: */
size_t sd_get_num() { return 1; }

sd_card_t *sd_get_by_num(size_t num) {
    if (0 == num)
        return &sd_card;
    else
        return NULL;
}

extern "C" {
volatile bool __otherCoreIdled = false;
};

[[noreturn]] void main1();

void initGpio() {
    bi_decl(bi_2pins_with_func(ESP_RX, ESP_TX, GPIO_FUNC_UART));

    gpio_set_function(ESP_RX, GPIO_FUNC_UART);
    gpio_set_function(ESP_TX, GPIO_FUNC_UART);
    //uart_set_hw_flow(ESP_UART, false, false);
    uart_init(ESP_UART, 115200);

    bi_decl(bi_2pins_with_func(CB_RX, CB_TX, GPIO_FUNC_UART));

    gpio_set_function(CB_RX, GPIO_FUNC_UART);
    gpio_set_inover(CB_RX, GPIO_OVERRIDE_INVERT);
    gpio_set_function(CB_TX, GPIO_FUNC_UART);
    gpio_set_outover(CB_TX, GPIO_OVERRIDE_INVERT);
    uart_init(CB_UART, 9600);

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(QWIIC1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(QWIIC1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(QWIIC1_SDA);
    gpio_pull_up(QWIIC1_SCL);

    i2c_init(i2c1, 100 * 1000);
    gpio_set_function(QWIIC2_SDA, GPIO_FUNC_I2C);
    gpio_set_function(QWIIC2_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(QWIIC2_SDA);
    gpio_pull_up(QWIIC2_SCL);

    spi_init(spi1, 500*1000);
    gpio_set_function(SETTINGS_FLASH_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(SETTINGS_FLASH_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SETTINGS_FLASH_MOSI, GPIO_FUNC_SPI);

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(SETTINGS_FLASH_CS);
    gpio_set_dir(SETTINGS_FLASH_CS, GPIO_OUT);
    gpio_put(SETTINGS_FLASH_CS, true);

#if defined(HARDWARE_REVISION_OPENLCC_R2A) || defined(HARDWARE_REVISION_OPENLCC_R2B)
    // ~WP and ~RESET are active-low, so we'll initialise them to a driven-high state
    gpio_init(SETTINGS_FLASH_WP_D2);
    gpio_set_dir(SETTINGS_FLASH_WP_D2, GPIO_OUT);
    gpio_put(SETTINGS_FLASH_WP_D2, true);

    gpio_init(SETTINGS_FLASH_RES_D3);
    gpio_set_dir(SETTINGS_FLASH_RES_D3, GPIO_OUT);
    gpio_put(SETTINGS_FLASH_RES_D3, true);
#endif

#if defined(HARDWARE_REVISION_OPENLCC_R1A) || defined(HARDWARE_REVISION_OPENLCC_R2A)
    // Ideally, SD_DET_A would be connected directly to ground, but *someone* (okay, me) didn't
    // read the datasheet on this. Making this pin ground and using a pull-up on the input
    // *should* be safe though.
    gpio_init(SD_DET_A);
    gpio_set_dir(SD_DET_A, GPIO_OUT);
    gpio_put(SD_DET_A, false);
#endif

    gpio_init(SD_DET_B);
    gpio_set_dir(SD_DET_B, GPIO_IN);
    gpio_pull_up(SD_DET_B);

#ifdef HARDWARE_REVISION_OPENLCC_R2A
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    // Let's light the LED immediately
    gpio_put(LED_PIN, true);
#endif
}

bool sd_card_inserted() {
    return !gpio_get(SD_DET_B);
}

int i2c_bus_scan(i2c_inst_t* i2c) {
    printf("\nI2C Bus Scan\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int addr = 0; addr < (1 << 7); ++addr) {
        if (addr % 16 == 0) {
            printf("%02x ", addr);
        }

        // Perform a 1-byte dummy read from the probe address. If a slave
        // acknowledges this address, the function returns the number of bytes
        // transferred. If the address byte is ignored, the function returns
        // -1.

        // Skip over any reserved addresses.
        int ret;
        uint8_t rxdata;
        if ((addr & 0x78) == 0 || (addr & 0x78) == 0x78)
            ret = PICO_ERROR_GENERIC;
        else
            ret = i2c_read_blocking(i2c, addr, &rxdata, 1, false);

        printf(ret < 0 ? "." : "@");
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
    printf("Done.\n");
    return 0;
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

        if (statusQueue->isFull())  {
            if (!core1RebootTimer.has_value()) {
                core1RebootTimer = make_timeout_time_ms(2000);
            }
        }
    }
}

[[noreturn]] void main1() {
    // Core 1 - ESP32 communication, saving things to SD-card, settings flash etc

    support.registerCore();
    SystemControllerStatusMessage sm;

    SystemControllerCommand beginCommand;
    beginCommand.type = COMMAND_BEGIN;
    commandQueue->tryAdd(&beginCommand);

    absolute_time_t nextSend = make_timeout_time_ms(2500);

    automations = new Automations(settingsManager, commandQueue);

    espFirmware = new EspFirmware(ESP_UART, commandQueue, status, settingsManager, automations);
    EspFirmware::initInterrupts(ESP_UART);

    i2c_bus_scan(i2c0);
    i2c_bus_scan(i2c1);

    mcp9600_0x60 = new MCP9600(i2c0, 0x60, MCP9600_PROBE_TYPE_K);
    if (mcp9600_0x60->isConnected()) {
        mcp9600_0x60->initialize();
    }

    mcp9600_0x67 = new MCP9600(i2c0, 0x67, MCP9600_PROBE_TYPE_K);
    if (mcp9600_0x67->isConnected()) {
        mcp9600_0x67->initialize();
    }

    mcp9600_0x63 = new MCP9600(i2c0, 0x63, MCP9600_PROBE_TYPE_K);
    if (mcp9600_0x63->isConnected()) {
        mcp9600_0x63->initialize();
    }

    if (sd_card_inserted()) {
        sd_card_t *pSD = sd_get_by_num(0);
        FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
        if (FR_OK != fr) {
            USB_PRINTF("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        } else {
            USB_PRINTF("SD Card mounted\n");
        }
    }

/*    // See FatFs - Generic FAT Filesystem Module, "Application Interface",
    // http://elm-chan.org/fsw/ff/00index_e.html
    sd_card_t *pSD = sd_get_by_num(0);
    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr) printf("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    FIL fil;
    const char* const filename = "filename2.txt";
    fr = f_open(&fil, filename, FA_OPEN_APPEND | FA_WRITE);
    if (FR_OK != fr && FR_EXIST != fr)
        printf("f_open(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);
    if (f_printf(&fil, "Hello, world!\n") < 0) {
        printf("f_printf failed\n");
    }
    fr = f_close(&fil);
    if (FR_OK != fr) {
        printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    f_unmount(pSD->pcName);
    */

    float externalTemp1 = 0.f;
    float externalTemp2 = 0.f;
    float externalTemp3 = 0.f;

    if (mcp9600_0x60->isConnected()) {
        printf("MCP9600 0x60 Connected\n");
    } else {
        printf("MCP9600 0x60 Not connected\n");
    }

    if (mcp9600_0x63->isConnected()) {
        printf("MCP9600 0x63 Connected\n");
    } else {
        printf("MCP9600 0x63 Not connected\n");
    }

    if (mcp9600_0x67->isConnected()) {
        printf("MCP9600 0x67 Connected\n");
    } else {
        printf("MCP9600 0x67 Not connected\n");
    }

    while (true) {
        while (!statusQueue->isEmpty()) {
            statusQueue->removeBlocking(&sm);
        }

        status->updateStatusMessage(sm);
        espFirmware->loop();
        automations->loop(sm);

        if (time_reached(nextSend)) {
            if (mcp9600_0x60->isConnected()) {
                externalTemp1 = mcp9600_0x60->readTemperature(0x40);
            }

            if (mcp9600_0x63->isConnected()) {
                externalTemp2 = mcp9600_0x63->readTemperature(0x40);
            }

            if (mcp9600_0x67->isConnected()) {
                externalTemp3 = mcp9600_0x67->readTemperature(0x40);
            }

            //USB_PRINTF("Sending status! Yay! Temp1: %.2f\n", externalTemp1);

            espFirmware->sendStatus(
                    &sm,
                    externalTemp1,
                    externalTemp2,
                    externalTemp3,
                    settingsManager->getAutoSleepMin(),
                    automations->getPlannedSleepInMinutes(),
                    automations->getCurrentlyLoadedRoutine(),
                    automations->getCurrentRoutineStep()
                    );
            nextSend = make_timeout_time_ms(250); // Normal 250

            settingsManager->writeSettingsIfChanged();
        }
    }

}

bool repeating_timer_callback([[maybe_unused]] repeating_timer_t *t) {
    systemController->sendSafePacketNoWait();
    return true;
}

int main() {
    INIT_USB_DEBUG();

    initGpio();

    USB_DEBUG_DELAY();

    support.begin(2);

    statusQueue = new PicoQueue<SystemControllerStatusMessage>(100);
    commandQueue = new PicoQueue<SystemControllerCommand>(100);

    settingsFlash = new SettingsFlash(spi1, SETTINGS_FLASH_CS);

    settingsManager = new SettingsManager(commandQueue, settingsFlash);
    settingsManager->initialize();

    systemController = new SystemController(CB_UART, statusQueue, commandQueue);
    add_repeating_timer_ms(1000, repeating_timer_callback, nullptr, &safePacketBootupTimer);

    status = new SystemStatus();

    multicore_reset_core1();
    multicore_launch_core1(main1);

    main0();
}
