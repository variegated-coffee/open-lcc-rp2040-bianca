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

    currentRoutine.emplace_back(); // Step 0

    auto fullFlow = SystemControllerCommand{
            .type = COMMAND_SET_FLOW_MODE,
            .int1 = PUMP_ON_SOLENOID_OPEN,
    };

    auto lowFlow = SystemControllerCommand{
            .type = COMMAND_SET_FLOW_MODE,
            .int1 = PUMP_OFF_SOLENOID_OPEN,
    };

    RoutineStep step1 = RoutineStep();
    step1.exitConditions.emplace_back(BREW_START, 0, 2);
    currentRoutine.push_back(step1);

    RoutineStep step2 = RoutineStep();
    step2.entryCommands.push_back(fullFlow);
    step2.exitConditions.emplace_back(BREW_TIME_ABSOLUTE, 4, 3);
    currentRoutine.push_back(step2);

    RoutineStep step3 = RoutineStep();
    step3.entryCommands.push_back(lowFlow);
    step3.exitConditions.emplace_back(STEP_TIME, 10, 4);
    currentRoutine.push_back(step3);

    RoutineStep step4 = RoutineStep();
    step4.entryCommands.push_back(fullFlow);
    currentRoutine.push_back(step4);
}

float Automations::getPlannedSleepInMinutes() {
    float sleepSeconds = plannedAutoSleepAt.has_value() ? (float)(absolute_time_diff_us(get_absolute_time(), plannedAutoSleepAt.value())) / 1000.f / 1000.f : INFINITY;
    if (sleepSeconds < 0) {
        sleepSeconds = 0.f;
    }

    return sleepSeconds;
}

void Automations::handleCurrentAutomationStep(SystemControllerStatusMessage sm) {
    if (currentAutomationStep >= currentRoutine.size()) {
        // This shouldn't happen, but let's just have an escape hatch
        moveToAutomationStep(0);
        return;
    }

    auto currentStep = currentRoutine.at(currentAutomationStep);

    for (auto exitCondition : currentStep.exitConditions) {
        //USB_PRINTF("Evaluating condition %u, value %f, exit: %u\n", exitCondition.type, exitCondition.value, exitCondition.exitToStep);
        switch (exitCondition.type) {
            case BREW_START:
                if (sm.currentlyBrewing) {
                    moveToAutomationStep(exitCondition.exitToStep);
                    return;
                }
                break;
            case BREW_TIME_ABSOLUTE:
                if (currentBrewTime() >= exitCondition.value) {
                    moveToAutomationStep(exitCondition.exitToStep);
                    return;
                }
                break;
            case STEP_TIME:
                if (currentStepTime() >= exitCondition.value) {
                    moveToAutomationStep(exitCondition.exitToStep);
                    return;
                }
                break;
        }
    }
}

void Automations::moveToAutomationStep(uint16_t step) {
    USB_PRINTF("Moving to automation step %u\n", step);
    if (step >= currentRoutine.size()) {
        // This shouldn't happen, but let's just have an escape hatch
        step = 0;
    }

    auto nextStep = currentRoutine.at(step);

    for (auto entryCommand : nextStep.entryCommands) {
        commandQueue->addBlocking(&entryCommand);
    }

    currentAutomationStep = step;
    currentStepStartedAt = get_absolute_time();

    if (step == 0) {
        unloadRoutine();
    }
}

void Automations::onBrewEnded() {
    brewStartedAt.reset();
    if (currentAutomationStep > 0) {
        moveToAutomationStep(0);
    }
}

void Automations::enqueueRoutine(uint32_t routineId) {
    currentlyLoadedRoutine = routineId;
    moveToAutomationStep(1);
}

void Automations::cancelRoutine() {
    moveToAutomationStep(0);
}

void Automations::exitingSleep() {
    resetPlannedSleep();
}

void Automations::unloadRoutine() {
    currentlyLoadedRoutine = 0;
    currentAutomationStep = 0;
    currentStepStartedAt.reset();
}


