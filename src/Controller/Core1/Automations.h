//
// Created by Magnus Nordlander on 2023-10-29.
//

#ifndef SMART_LCC_AUTOMATIONS_H
#define SMART_LCC_AUTOMATIONS_H

#include <optional.hpp>
#include <pico/time.h>
#include <vector>
#include "SettingsManager.h"
#include "Routine/RoutineStep.h"

class Automations {
public:
    Automations(SettingsManager* settingsManager, PicoQueue<SystemControllerCommand> *commandQueue);

    void loop(SystemControllerStatusMessage sm);

    float getPlannedSleepInMinutes();
    [[nodiscard]] inline uint16_t getCurrentlyLoadedRoutine() const {
        return currentlyLoadedRoutine;
    }

    [[nodiscard]] inline uint16_t getCurrentRoutineStep() const {
        return currentAutomationStep;
    }

    void enqueueRoutine(uint32_t routineId);
    void cancelRoutine();

    void exitingSleep();

private:
    void onBrewStarted();
    void onBrewEnded();
    void resetPlannedSleep();

    void unloadRoutine();

    void handleCurrentAutomationStep(SystemControllerStatusMessage sm);
    void moveToAutomationStep(uint16_t step);

    inline float currentBrewTime() {
        return brewStartedAt.has_value() ? (float)(absolute_time_diff_us(brewStartedAt.value(), get_absolute_time())) / 1000.f / 1000.f : 0.f;
    }

    inline float currentStepTime() {
        return currentStepStartedAt.has_value() ? (float)(absolute_time_diff_us(currentStepStartedAt.value(), get_absolute_time())) / 1000.f / 1000.f : 0.f;
    }

    nonstd::optional<absolute_time_t> plannedAutoSleepAt{};
    nonstd::optional<absolute_time_t> brewStartedAt{};

    SettingsManager* settingsManager;
    PicoQueue<SystemControllerCommand> *commandQueue;

    bool previouslyAsleep;
    bool previouslyBrewing = false;
    uint16_t previousAutosleepMinutes = 0;

    uint16_t currentAutomationStep = 0;
    nonstd::optional<absolute_time_t> currentStepStartedAt{};

    uint16_t currentlyLoadedRoutine = 0;

    // Reserved steps:
    // 0: Routine not loaded. Commands should reset machine.
    //    When brewing stops, we always revert to step 0.
    // 1: Routine enqueued. Should exit on brew start.
    std::vector<RoutineStep> currentRoutine;
};


#endif //SMART_LCC_AUTOMATIONS_H
