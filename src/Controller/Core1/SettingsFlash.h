//
// Created by Magnus Nordlander on 2023-10-22.
//

#ifndef SMART_LCC_SETTINGSFLASH_H
#define SMART_LCC_SETTINGSFLASH_H

#include <hardware/spi.h>
#include <pico/time.h>

#define SETTINGS_FLASH_PAGE_SIZE        256
#define SETTINGS_FLASH_SECTOR_SIZE      4096

class SettingsFlash {
public:
    SettingsFlash(spi_inst_t* spi, uint csPin);

    void read(uint32_t addr, uint8_t *buf, size_t len);
    void write_enable();
    void sector_erase(uint32_t addr);
    void page_program(uint32_t addr, uint8_t *buf, size_t len);
    uint8_t get_manufacturer_id();
    uint16_t get_device_id();
    bool is_present();
private:
    void wait_done();
    spi_inst_t* _spi;
    uint _csPin;
};


#endif //SMART_LCC_SETTINGSFLASH_H
