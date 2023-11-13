//
// Created by Magnus Nordlander on 2023-11-11.
//

#ifndef RP2040_BIANCA_CORE1STATE_H
#define RP2040_BIANCA_CORE1STATE_H


#include <cstdint>

class Core1State {
public:
    float externalTemp1;
    float externalTemp2;
    float externalTemp3;
    uint16_t autoSleepMinutes;
    float plannedSleepInMinutes;
    uint16_t currentlyLoadedRoutine;
    uint16_t currentRoutineStep;
};


#endif //RP2040_BIANCA_CORE1STATE_H
