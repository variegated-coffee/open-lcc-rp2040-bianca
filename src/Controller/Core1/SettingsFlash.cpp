//
// Created by Magnus Nordlander on 2023-10-22.
//

#include "SettingsFlash.h"
#include <hardware/gpio.h>
#include <cstring>

#define FLASH_CMD_WRITE_STATUS_REGISTER_1       0x05
#define FLASH_CMD_PAGE_PROGRAM 0x02
#define FLASH_CMD_READ         0x03
#define FLASH_CMD_WRITE_DISABLE 0x04
#define FLASH_CMD_READ_STATUS_REGISTER_1       0x05
#define FLASH_CMD_WRITE_ENABLE     0x06
#define FLASH_CMD_FAST_READ 0x0B
#define FLASH_CMD_WRITE_STATUS_REGISTER_3       0x11
#define FLASH_CMD_READ_STATUS_REGISTER_3       0x15
#define FLASH_CMD_SECTOR_ERASE 0x20
#define FLASH_CMD_WRITE_STATUS_REGISTER_2       0x31
#define FLASH_CMD_READ_STATUS_REGISTER_2       0x35
#define FLASH_CMD_READ_UNIQUE_ID 0x4B
#define FLASH_CMD_VOLATILE_SR_WRITE_ENABLE 0x50
#define FLASH_CMD_BLOCK_ERASE_32K 0x52
#define FLASH_CMD_CHIP_ERASE 0x60
#define FLASH_CMD_MANUFACTURER_ID 0x90
#define FLASH_CMD_JEDEC_ID 0x9F
#define FLASH_CMD_RELEASE_POWER_DOWN 0xAB
#define FLASH_CMD_BLOCK_ERASE_64K 0xD8

#define FLASH_STATUS_BUSY_MASK 0x01

SettingsFlash::SettingsFlash(spi_inst_t *spi, uint csPin): _spi(spi), _csPin(csPin) {

}

static inline void cs_select(uint _csPin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(_csPin, 0);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint _csPin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(_csPin, 1);
    asm volatile("nop \n nop \n nop"); // FIXME
}

void SettingsFlash::read(uint32_t addr, uint8_t *buf, size_t len) {
    cs_select(_csPin);
    uint8_t cmdbuf[4] = {
            FLASH_CMD_READ,
            addr >> 16,
            addr >> 8,
            addr
    };
    spi_write_blocking(_spi, cmdbuf, 4);
    spi_read_blocking(_spi, 0, buf, len);
    cs_deselect(_csPin);
}

void SettingsFlash::write_enable() {
    cs_select(_csPin);
    uint8_t cmd = FLASH_CMD_WRITE_ENABLE;
    spi_write_blocking(_spi, &cmd, 1);
    cs_deselect(_csPin);
}

void SettingsFlash::wait_done() {
    uint8_t status;
    do {
        cs_select(_csPin);
        uint8_t buf[2] = {FLASH_CMD_READ_STATUS_REGISTER_1, 0};
        spi_write_read_blocking(_spi, buf, buf, 2);
        cs_deselect(_csPin);
        status = buf[1];
    } while (status & FLASH_STATUS_BUSY_MASK);
}

void SettingsFlash::sector_erase(uint32_t addr) {
    uint8_t cmdbuf[4] = {
            FLASH_CMD_SECTOR_ERASE,
            addr >> 16,
            addr >> 8,
            addr
    };
    write_enable();
    cs_select(_csPin);
    spi_write_blocking(_spi, cmdbuf, 4);
    cs_deselect(_csPin);
    wait_done();
}

void SettingsFlash::page_program(uint32_t addr, uint8_t *buf, size_t len) {
    uint8_t pageBuf[SETTINGS_FLASH_PAGE_SIZE] = {0};

    if (len <= SETTINGS_FLASH_PAGE_SIZE) {
        memcpy(pageBuf, buf, len);
    }

    uint8_t cmdbuf[4] = {
            FLASH_CMD_PAGE_PROGRAM,
            addr >> 16,
            addr >> 8,
            addr
    };
    write_enable();
    wait_done();
    cs_select(_csPin);
    spi_write_blocking(_spi, cmdbuf, 4);
    spi_write_blocking(_spi, pageBuf, SETTINGS_FLASH_PAGE_SIZE);
    cs_deselect(_csPin);
    wait_done();
}

uint8_t SettingsFlash::get_manufacturer_id() {
    uint8_t cmdbuf[5] = {
            FLASH_CMD_MANUFACTURER_ID,
            0,
            0,
            0,
            0
    };
    cs_select(_csPin);
    spi_write_read_blocking(_spi, cmdbuf, cmdbuf, 5);
    cs_deselect(_csPin);
    wait_done();
    return cmdbuf[4];
}

uint16_t SettingsFlash::get_device_id() {
    uint8_t cmdbuf[4] = {
            FLASH_CMD_JEDEC_ID,
            0,
            0,
            0,
    };
    cs_select(_csPin);
    spi_write_read_blocking(_spi, cmdbuf, cmdbuf, 5);
    cs_deselect(_csPin);
    wait_done();

    return (cmdbuf[2] << 8) + cmdbuf[3];
}

bool SettingsFlash::is_present() {
    return get_manufacturer_id() != 0x00;
}
