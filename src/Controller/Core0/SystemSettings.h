//
// Created by Magnus Nordlander on 2021-08-20.
//

#ifndef FIRMWARE_SYSTEMSETTINGS_H
#define FIRMWARE_SYSTEMSETTINGS_H

#include "utils/PicoQueue.h"
#include "types.h"
#include "MulticoreSupport.h"

class SystemSettings {
public:
    [[nodiscard]] inline float getBrewTemperatureOffset() const { return currentSettings.brewTemperatureOffset; };
    [[nodiscard]] inline bool getEcoMode() const { return currentSettings.ecoMode; };
    [[nodiscard]] inline bool getSleepMode() const { return currentSettings.sleepMode; };
    [[nodiscard]] inline bool getSteamOnlyMode() const { return currentSettings.steamOnlyMode; };
    [[nodiscard]] inline bool getStandbyMode() const { return currentSettings.standbyMode; };
    [[nodiscard]] inline float getTargetBrewTemp() const { return currentSettings.brewTemperatureTarget; };
    [[nodiscard]] inline float getTargetServiceTemp() const { return currentSettings.serviceTemperatureTarget; };
    [[nodiscard]] inline PidSettings getBrewPidParameters() const { return currentSettings.brewPidParameters; };
    [[nodiscard]] inline PidSettings getServicePidParameters() const { return currentSettings.servicePidParameters; };

    inline void setBrewTemperatureOffset(float offset) { currentSettings.brewTemperatureOffset = offset; };
    inline void setEcoMode(bool ecoMode) { currentSettings.ecoMode = ecoMode; };
    inline void setSteamOnlyMode(bool steamOnlyMode) { currentSettings.steamOnlyMode = steamOnlyMode; };
    inline void setSleepMode(bool sleepMode) { currentSettings.sleepMode = sleepMode; };
    inline void setStandbyMode(bool standbyMode) { currentSettings.standbyMode = standbyMode; };
    inline void setTargetBrewTemp(float targetBrewTemp) { currentSettings.brewTemperatureTarget = targetBrewTemp; };
    inline void setOffsetTargetBrewTemp(float offsetTargetBrewTemp) { setTargetBrewTemp(offsetTargetBrewTemp - currentSettings.brewTemperatureOffset); };
    inline void setTargetServiceTemp(float targetServiceTemp) { currentSettings.serviceTemperatureTarget = targetServiceTemp; };
    inline void setBrewPidParameters(PidSettings params) { currentSettings.brewPidParameters = params; };
    inline void setServicePidParameters(PidSettings params) { currentSettings.servicePidParameters = params; };
private:
    SettingStruct currentSettings;
};


#endif //FIRMWARE_SYSTEMSETTINGS_H
