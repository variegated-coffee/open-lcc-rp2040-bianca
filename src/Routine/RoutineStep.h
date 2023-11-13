//
// Created by Magnus Nordlander on 2023-11-09.
//

#ifndef RP2040_BIANCA_ROUTINESTEP_H
#define RP2040_BIANCA_ROUTINESTEP_H


#include <vector>
#include "RoutineStepExitCondition.h"
#include <types.h>

class RoutineStep {
public:
    std::vector<RoutineStepExitCondition> exitConditions;
    std::vector<SystemControllerCommand> entryCommands;
};


#endif //RP2040_BIANCA_ROUTINESTEP_H
