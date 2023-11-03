//
// Created by Magnus Nordlander on 2023-10-29.
//

#ifndef SMART_LCC_AUTOMATIONS_H
#define SMART_LCC_AUTOMATIONS_H

#include <optional.hpp>
#include <pico/time.h>
#include "SettingsManager.h"

class Automations {
public:
    Automations(SettingsManager* settingsManager, PicoQueue<SystemControllerCommand> *commandQueue);

    void loop(SystemControllerStatusMessage sm);

    float getPlannedSleepInMinutes();

    void enqueueRoutine(uint32_t routineId);
    void cancelRoutine();

    void exitingSleep();

private:
    void onBrewStarted();
    void onBrewEnded();
    void resetPlannedSleep();

    void handleCurrentAutomationStep(SystemControllerStatusMessage sm);
    void moveToAutomationStep(uint16_t step);

    nonstd::optional<absolute_time_t> plannedAutoSleepAt{};
    nonstd::optional<absolute_time_t> brewStartedAt{};

    SettingsManager* settingsManager;
    PicoQueue<SystemControllerCommand> *commandQueue;

    bool previouslyAsleep;
    bool previouslyBrewing = false;
    uint16_t previousAutosleepMinutes = 0;

    uint16_t currentAutomationStep = 0;
};


#endif //SMART_LCC_AUTOMATIONS_H
