//
// Created by Magnus Nordlander on 2023-10-23.
//

#include <cstring>
#include "SettingsManager.h"
#include "utils/crc32.h"
#include "hardware/watchdog.h"
#include "utils/USBDebug.h"

#define SETTINGS_CURRENT_VERSION 0x01
#define SETTINGS_ADDR 0x00000000

struct SettingsHeader{
    uint8_t version;
    crc32_t crc;
    size_t len;
};

const SettingStruct defaultSettings{
        .brewTemperatureOffset = -10,
        .sleepMode = false,
        .ecoMode = false,
        .brewTemperatureTarget = 105,
        .serviceTemperatureTarget = 120,
        .autoSleepMin = 0,
        .brewPidParameters = PidSettings{.Kp = 0.8, .Ki = 0.12, .Kd = 12.0, .windupLow = -7.f, .windupHigh = 7.f},
        .servicePidParameters = PidSettings{.Kp = 0.6, .Ki = 0.1, .Kd = 1.0, .windupLow = -10.f, .windupHigh = 10.f},
};

SettingsManager::SettingsManager(PicoQueue<SystemControllerCommand> *commandQueue, SettingsFlash* settingsFlash): commandQueue(commandQueue), settingsFlash(settingsFlash) {

}

void SettingsManager::sendMessage(SystemControllerCommand command) {
    commandQueue->tryAdd(&command);
}

void SettingsManager::setBrewTemperatureOffset(float offset)
{
    currentSettings.brewTemperatureOffset = offset;
    sendMessage(SystemControllerCommand{
            .type = COMMAND_SET_BREW_OFFSET,
            .float1 = offset,
    });
}

void SettingsManager::setEcoMode(bool ecoMode)
{
    currentSettings.ecoMode = ecoMode;
    sendMessage(SystemControllerCommand{
            .type = COMMAND_SET_ECO_MODE,
            .bool1 = ecoMode,
    });
}

void SettingsManager::setTargetBrewTemp(float targetBrewTemp)
{
    currentSettings.brewTemperatureTarget = targetBrewTemp;
    sendMessage(SystemControllerCommand{
            .type = COMMAND_SET_BREW_SET_POINT,
            .float1 = targetBrewTemp,
    });
}

void SettingsManager::setAutoSleepMin(uint16_t minutes)
{
    currentSettings.autoSleepMin = minutes;
    sendMessage(SystemControllerCommand{
            .type = COMMAND_SET_AUTO_SLEEP_MINUTES,
            .float1 = static_cast<float>(minutes),
    });
}

void SettingsManager::setOffsetTargetBrewTemp(float offsetTargetBrewTemp) {
    setTargetBrewTemp(offsetTargetBrewTemp - currentSettings.brewTemperatureOffset);
}

void SettingsManager::setTargetServiceTemp(float targetServiceTemp)
{
    currentSettings.serviceTemperatureTarget = targetServiceTemp;
    sendMessage(SystemControllerCommand{
            .type = COMMAND_SET_SERVICE_SET_POINT,
            .float1 = targetServiceTemp,
    });
}

void SettingsManager::setBrewPidParameters(PidSettings params)
{
    currentSettings.brewPidParameters = params;
    sendMessage(SystemControllerCommand{
            .type = COMMAND_SET_BREW_PID_PARAMETERS,
            .float1 = params.Kp,
            .float2 = params.Ki,
            .float3 = params.Kd,
            .float4 = params.windupLow,
            .float5 = params.windupHigh,
    });
}

void SettingsManager::setServicePidParameters(PidSettings params)
{
    currentSettings.servicePidParameters = params;
    sendMessage(SystemControllerCommand{
            .type = COMMAND_SET_SERVICE_PID_PARAMETERS,
            .float1 = params.Kp,
            .float2 = params.Ki,
            .float3 = params.Kd,
            .float4 = params.windupLow,
            .float5 = params.windupHigh,
    });
}

void SettingsManager::setSleepMode(bool sleepMode)
{
    currentSettings.sleepMode = sleepMode;
    sendMessage(SystemControllerCommand{
        .type = COMMAND_SET_SLEEP_MODE,
        .bool1 = sleepMode
    });
}

void SettingsManager::initialize() {
    readSettings();

    // If we've reset due to the watchdog or for some other reason, use the previous sleep mode setting, otherwise reset it to false
    if (currentSettings.sleepMode && !watchdog_enable_caused_reboot() && to_ms_since_boot(get_absolute_time()) < 20000) {
        currentSettings.sleepMode = false;
    }

    sendAllSettings();
}

void SettingsManager::readSettings() {
    static_assert(sizeof(SettingsHeader) + sizeof(SettingStruct) <= SETTINGS_FLASH_PAGE_SIZE);

    currentSettings = SettingStruct{};

    if (!settingsFlash->is_present()) {
        USB_PRINTF("Settings flash is not present\n");
        memcpy(&currentSettings, &defaultSettings, sizeof(SettingStruct));
        return;
    } else {
        uint16_t dev = settingsFlash->get_device_id();
        USB_PRINTF("Settings flash preset, device id %x\n", dev);
    }

    uint8_t page[SETTINGS_FLASH_PAGE_SIZE];

    settingsFlash->read(SETTINGS_ADDR, page, SETTINGS_FLASH_PAGE_SIZE);

    USB_PRINTF("Settings flash contents:\n");
    for (int i = 0; i < SETTINGS_FLASH_PAGE_SIZE; ++i) {
        if (i % 16 == 15)
            USB_PRINTF("%02x\n", page[i]);
        else
            USB_PRINTF("%02x ", page[i]);
    }
    USB_PRINTF("\n");

    SettingsHeader header{};
    SettingStruct read{};
    memcpy(&header, page, sizeof(SettingsHeader));
    memcpy(&read, page + sizeof(SettingsHeader), sizeof(SettingStruct));

    if (header.version == SETTINGS_CURRENT_VERSION && header.len == sizeof(SettingStruct)) {
        crc32_t readCrc;
        crc32(&read, sizeof(SettingStruct), &readCrc);

        if (readCrc == header.crc) {
            USB_PRINTF("Using read settings\n");

            memcpy(&lastReadSettings, &read, sizeof(SettingStruct));
            memcpy(&currentSettings, &read, sizeof(SettingStruct));
            return;
        }
    }

    USB_PRINTF("Using default settings\n");
    memcpy(&currentSettings, &defaultSettings, sizeof(SettingStruct));
}

void SettingsManager::writeSettingsIfChanged() {
    if (memcmp(&currentSettings, &lastReadSettings, sizeof(SettingStruct)) != 0) {
        writeToFlash();
    }
}

void SettingsManager::writeToFlash() {
    USB_PRINTF("Writing settings to flash\n");

    crc32_t crc;
    crc32(&currentSettings, sizeof(SettingStruct), &crc);

    static_assert(sizeof(SettingsHeader) + sizeof(SettingStruct) <= SETTINGS_FLASH_PAGE_SIZE);

    uint8_t paddedData[SETTINGS_FLASH_PAGE_SIZE]{0};

    SettingsHeader header{
            .version = SETTINGS_CURRENT_VERSION,
            .crc = crc,
            .len = sizeof(SettingStruct),
    };

    memcpy(paddedData, &header, sizeof(SettingsHeader));
    memcpy(paddedData + sizeof(SettingsHeader), &currentSettings, sizeof(SettingStruct));

    settingsFlash->sector_erase(SETTINGS_ADDR);
    settingsFlash->page_program(SETTINGS_ADDR, paddedData, SETTINGS_FLASH_PAGE_SIZE);

    readSettings();
}

void SettingsManager::sendAllSettings() {
    setBrewTemperatureOffset(currentSettings.brewTemperatureOffset);
    setEcoMode(currentSettings.ecoMode);
    setTargetBrewTemp(currentSettings.brewTemperatureTarget);
    setAutoSleepMin(currentSettings.autoSleepMin);
    setTargetServiceTemp(currentSettings.serviceTemperatureTarget);
    setBrewPidParameters(currentSettings.brewPidParameters);
    setServicePidParameters(currentSettings.servicePidParameters);
    setSleepMode(currentSettings.sleepMode);
}
