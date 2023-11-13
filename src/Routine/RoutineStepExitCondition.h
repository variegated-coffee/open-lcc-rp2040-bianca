//
// Created by Magnus Nordlander on 2023-11-09.
//

#ifndef RP2040_BIANCA_ROUTINESTEPEXITCONDITION_H
#define RP2040_BIANCA_ROUTINESTEPEXITCONDITION_H


#include <cstdint>

typedef enum {
    BREW_START,
    BREW_TIME_ABSOLUTE,
    STEP_TIME,
} RoutineStepExitConditionType;

class RoutineStepExitCondition {
public:
    explicit RoutineStepExitCondition(RoutineStepExitConditionType type, float value, uint16_t exitToStep) :
        type(type),
        value(value),
        exitToStep(exitToStep) {}

    RoutineStepExitConditionType type;
    float value;
    uint16_t exitToStep;
};


#endif //RP2040_BIANCA_ROUTINESTEPEXITCONDITION_H
