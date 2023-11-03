//
// Created by Magnus Nordlander on 2023-10-23.
//

#ifndef SMART_LCC_SETTINGSMANAGER_H
#define SMART_LCC_SETTINGSMANAGER_H

#include "types.h"
#include "utils/PicoQueue.h"
#include "SettingsFlash.h"

class SettingsManager {
public:
    explicit SettingsManager(PicoQueue<SystemControllerCommand> *commandQueue, SettingsFlash* settingsFlash);

    void initialize();

    void setBrewTemperatureOffset(float offset);
    void setEcoMode(bool ecoMode);
    void setTargetBrewTemp(float targetBrewTemp);
    void setAutoSleepMin(uint16_t minutes);
    void setOffsetTargetBrewTemp(float offsetTargetBrewTemp);
    void setTargetServiceTemp(float targetServiceTemp);
    void setBrewPidParameters(PidSettings params);
    void setServicePidParameters(PidSettings params);
    void setSleepMode(bool sleepMode);

    inline float getBrewTemperatureOffset() const { return currentSettings.brewTemperatureOffset; };
    inline bool getEcoMode() const { return currentSettings.ecoMode; };
    inline bool getSleepMode() const { return currentSettings.sleepMode; };
    inline float getTargetBrewTemp() const { return currentSettings.brewTemperatureTarget; };
    inline uint16_t getAutoSleepMin() const { return currentSettings.autoSleepMin; };
    inline float getOffsetTargetBrewTemp() const { return currentSettings.brewTemperatureTarget + currentSettings.brewTemperatureOffset; };
    inline float getTargetServiceTemp() const { return currentSettings.serviceTemperatureTarget; };
    inline PidSettings getBrewPidParameters() const { return currentSettings.brewPidParameters; };
    inline PidSettings getServicePidParameters() const { return currentSettings.servicePidParameters; };

    void writeSettingsIfChanged();
private:
    PicoQueue<SystemControllerCommand> *commandQueue;
    SettingsFlash* settingsFlash;

    SettingStruct lastReadSettings{
        .brewTemperatureTarget = 33
    };
    SettingStruct currentSettings;

    void readSettings();
    void writeToFlash();
    void sendAllSettings();

    inline void sendMessage(SystemControllerCommand command);
};

#endif //SMART_LCC_SETTINGSMANAGER_H
