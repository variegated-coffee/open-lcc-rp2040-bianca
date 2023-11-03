//
// Created by Magnus Nordlander on 2023-10-29.
//

#include <cmath>
#include "Automations.h"
#include "utils/USBDebug.h"

void Automations::loop(SystemControllerStatusMessage sm) {
    if (!plannedAutoSleepAt.has_value()) {
        resetPlannedSleep();
    } else if (!settingsManager->getSleepMode() && time_reached(plannedAutoSleepAt.value())) {
        settingsManager->setSleepMode(true);
    }

    if (sm.currentlyBrewing && !previouslyBrewing) {
        onBrewStarted();
    } else if (previouslyBrewing && !sm.currentlyBrewing) {
        onBrewEnded();
    }

    if (!sm.sleepMode && previouslyAsleep) {
        resetPlannedSleep();
    }

    if (previousAutosleepMinutes != settingsManager->getAutoSleepMin()) {
        resetPlannedSleep();
    }

    previouslyBrewing = sm.currentlyBrewing;
    previouslyAsleep = sm.sleepMode;
    previousAutosleepMinutes = settingsManager->getAutoSleepMin();

    handleCurrentAutomationStep(sm);
}

void Automations::resetPlannedSleep() {
    if (settingsManager->getAutoSleepMin() > 0) {
        uint32_t ms = (uint32_t)settingsManager->getAutoSleepMin() * 60 * 1000;
        plannedAutoSleepAt = delayed_by_ms(get_absolute_time(), ms);
    } else {
        plannedAutoSleepAt.reset();
    }
}

void Automations::onBrewStarted() {
    brewStartedAt = get_absolute_time();

    // Starting a brew exits sleep mode
    if (settingsManager->getSleepMode()) {
        settingsManager->setSleepMode(false);
    }

    resetPlannedSleep();
}

Automations::Automations(SettingsManager *settingsManager, PicoQueue<SystemControllerCommand> *commandQueue): settingsManager(settingsManager), commandQueue(commandQueue) {
    previouslyAsleep = settingsManager->getSleepMode();
    previousAutosleepMinutes = settingsManager->getAutoSleepMin();
}

float Automations::getPlannedSleepInMinutes() {
    float sleepSeconds = plannedAutoSleepAt.has_value() ? (float)(absolute_time_diff_us(get_absolute_time(), plannedAutoSleepAt.value())) / 1000.f / 1000.f : INFINITY;
    if (sleepSeconds < 0) {
        sleepSeconds = 0.f;
    }

    return sleepSeconds;
}

void Automations::handleCurrentAutomationStep(SystemControllerStatusMessage sm) {
    switch (currentAutomationStep) {
        case 0: break;
        case 1: {
            if (sm.currentlyBrewing) {
                moveToAutomationStep(2);
            }
            break;
        }
        case 2: {
            float brewSeconds = brewStartedAt.has_value() ? (float)(absolute_time_diff_us(brewStartedAt.value(), get_absolute_time())) / 1000.f / 1000.f : 0;

            USB_PRINTF("Brew time: %f\n", brewSeconds);

            if (brewSeconds > 10.f) {
                moveToAutomationStep(3);
            }
            break;
        }
        case 3: {
            if (!sm.currentlyBrewing) {
                moveToAutomationStep(0);
            }
        }
    }
}

void Automations::moveToAutomationStep(uint16_t step) {
    USB_PRINTF("Moving to step %u\n", step);

    switch (step) {
        case 1: {
            auto lowFlow = SystemControllerCommand{
                    .type = COMMAND_SET_FLOW_MODE,
                    .int1 = PUMP_OFF_SOLENOID_OPEN,
            };
            commandQueue->addBlocking(&lowFlow);
            break;
        }
        case 2: break;
        case 3: {
            auto fullFlow = SystemControllerCommand{
                    .type = COMMAND_SET_FLOW_MODE,
                    .int1 = FULL_FLOW,
            };
            commandQueue->addBlocking(&fullFlow);
            break;
        }
        case 0: {
            // Reset everything
        }
    }

    if (step <= 4) {
        currentAutomationStep = step;
    }
}

void Automations::onBrewEnded() {
    brewStartedAt.reset();
}

void Automations::enqueueRoutine(uint32_t routineId) {
    moveToAutomationStep(1);
}

void Automations::cancelRoutine() {
    moveToAutomationStep(0);
}

void Automations::exitingSleep() {
    resetPlannedSleep();
}


