//
// Created by Magnus Nordlander on 2022-12-31.
//

#ifndef SMART_LCC_ESP_PROTOCOL_H
#define SMART_LCC_ESP_PROTOCOL_H

#include <cstdint>

#define ESP_RP2040_PROTOCOL_VERSION 0x0005

enum ESPMessageType: uint32_t {
    ESP_MESSAGE_PING = 0x00000001, // ESP -> RP2040
    ESP_MESSAGE_PONG, // RP2040 -> ESP
    ESP_MESSAGE_ACK, // RP2040 -> ESP
    ESP_MESSAGE_NACK, // RP2040 -> ESP
    ESP_MESSAGE_SYSTEM_STATUS, // RP2040 -> ESP
    ESP_MESSAGE_SYSTEM_COMMAND, // ESP -> RP2040
    ESP_MESSAGE_NOT_USED,
    ESP_MESSAGE_POLL_STATUS, // ESP -> RP2040
    ESP_MESSAGE_ADD_COMMAND_TO_ROUTINE_STEP, // ESP -> RP2040
    ESP_MESSAGE_ADD_EXIT_CONDITION_TO_ROUTINE_STEP, // ESP -> RP2040
};

enum ESPDirection: uint32_t {
    ESP_DIRECTION_RP2040_TO_ESP32 = 0x00000001,
    ESP_DIRECTION_ESP32_TO_RP2040
};

#define ESP_WARNING_LEVEL 0x100
#define ESP_DEBUG_LEVEL 0x400

enum ESPError: uint32_t {
    ESP_ERROR_NONE = 0x0,
    ESP_ERROR_INCOMPLETE_DATA = 0x01,
    ESP_ERROR_INVALID_CHECKSUM,
    ESP_ERROR_UNEXPECTED_MESSAGE_LENGTH,
    ESP_ERROR_PING_WRONG_VERSION = 0x05,
    ESP_ERROR_VALVE_CLOSED,
    ESP_WARNING_BAILED_CB_UNRESPONSIVE = 0x100,
};

enum ESPFlowMode: uint8_t {
    ESP_FLOW_MODE_FULL_FLOW = 0,
    ESP_FLOW_MODE_PUMP_ON_PWM_SOLENOID = 1,
    ESP_FLOW_MODE_PUMP_OFF_PWM_SOLENOID = 2,
    ESP_FLOW_MODE_PUMP_OFF_SOLENOID_OPEN = 3,
    ESP_FLOW_MODE_PUMP_OFF_SOLENOID_CLOSED = 4,
};

struct __attribute__((packed)) ESPMessageHeader {
    ESPDirection direction;
    uint32_t id;
    uint32_t responseTo;
    ESPMessageType type;
    ESPError error;
    uint16_t version;
    uint32_t length;
};

struct ESPPingMessage {
    uint16_t version = ESP_RP2040_PROTOCOL_VERSION;
};

struct ESPPongMessage {
    uint16_t version = ESP_RP2040_PROTOCOL_VERSION;
};

enum ESPSystemInternalState: uint8_t {
    ESP_SYSTEM_INTERNAL_STATE_NOT_STARTED_YET,
    ESP_SYSTEM_INTERNAL_STATE_RUNNING,
    ESP_SYSTEM_INTERNAL_STATE_SOFT_BAIL,
    ESP_SYSTEM_INTERNAL_STATE_HARD_BAIL,
};

enum ESPSystemRunState: uint8_t {
    ESP_SYSTEM_RUN_STATE_UNDETEMINED,
    ESP_SYSTEM_RUN_STATE_NORMAL,
    ESP_SYSTEM_RUN_STATE_HEATUP_STAGE_1,
    ESP_SYSTEM_RUN_STATE_HEATUP_STAGE_2,
    ESP_SYSTEM_RUN_STATE_FIRST_RUN,
};

enum ESPSystemCoalescedState: uint8_t {
    ESP_SYSTEM_COALESCED_STATE_UNDETERMINED = 0,
    ESP_SYSTEM_COALESCED_STATE_HEATUP,
    ESP_SYSTEM_COALESCED_STATE_TEMPS_NORMALIZING,
    ESP_SYSTEM_COALESCED_STATE_WARM,
    ESP_SYSTEM_COALESCED_STATE_SLEEPING,
    ESP_SYSTEM_COALESCED_STATE_BAILED,
    ESP_SYSTEM_COALESCED_STATE_FIRST_RUN
};

struct __attribute__((packed)) ESPSystemStatusMessage {
    ESPSystemInternalState internalState;
    ESPSystemRunState runState;
    ESPSystemCoalescedState coalescedState;
    float brewBoilerTemperature;
    float brewBoilerSetPoint;
    float serviceBoilerTemperature;
    float serviceBoilerSetPoint;
    float brewTemperatureOffset;
    uint16_t autoSleepAfter;
    bool currentlyBrewing;
    bool currentlyFillingServiceBoiler;
    bool ecoMode;
    bool sleepMode;
    bool waterTankLow;
    uint16_t plannedAutoSleepInSeconds;
    float rp2040Temperature;
    uint16_t numBails;
    uint32_t rp2040UptimeSeconds;
    uint16_t sbRawHi;
    uint16_t sbRawLo;
    float externalTemperature1;
    float externalTemperature2;
    float externalTemperature3;
    uint8_t flowMode;
    bool brewBoilerOn;
    bool serviceBoilerOn;
    uint16_t loadedRoutine;
    uint16_t currentRoutineStep;
    /*
     * To add:
     * Pid settings and pid parameters
     */
};

enum ESPSystemCommandType: uint32_t {
    ESP_SYSTEM_COMMAND_SET_BREW_SET_POINT,
    ESP_SYSTEM_COMMAND_SET_BREW_PID_PARAMETERS,
    ESP_SYSTEM_COMMAND_SET_BREW_OFFSET,
    ESP_SYSTEM_COMMAND_SET_SERVICE_SET_POINT,
    ESP_SYSTEM_COMMAND_SET_SERVICE_PID_PARAMETERS,
    ESP_SYSTEM_COMMAND_SET_ECO_MODE,
    ESP_SYSTEM_COMMAND_SET_SLEEP_MODE,
    ESP_SYSTEM_COMMAND_SET_AUTO_SLEEP_MINUTES,
    ESP_SYSTEM_COMMAND_SET_FLOW_MODE,
    ESP_SYSTEM_COMMAND_ENQUEUE_ROUTINE,
    ESP_SYSTEM_COMMAND_CANCEL_ROUTINE,
    ESP_SYSTEM_COMMAND_FORCE_HARD_BAIL,
    ESP_SYSTEM_COMMAND_CLEAR_ROUTINE,
};

struct __attribute__((packed)) ESPSystemCommandPayload {
    ESPSystemCommandType type;
    bool bool1;
    float float1;
    float float2;
    float float3;
    float float4;
    float float5;
    uint32_t int1;
    uint32_t int2;
    uint32_t int3;
};

struct __attribute__((packed)) ESPSystemCommandMessage {
    uint32_t checksum;
    ESPSystemCommandPayload payload;
};

enum ESPRoutineStateExitConditionType: uint8_t {
    ESP_ROUTINE_STATE_EXIT_CONDITION_TYPE_BREW_TIME = 0,
    ESP_ROUTINE_STATE_EXIT_CONDITION_TYPE_STEP_TIME,
    ESP_ROUTINE_STATE_EXIT_CONDITION_TYPE_BREW_START,
    ESP_ROUTINE_STATE_EXIT_CONDITION_TYPE_PRESSURE,
    ESP_ROUTINE_STATE_EXIT_CONDITION_TYPE_WEIGHT,
};

struct __attribute__((packed)) ESPAddCommandToRoutineStepMessage {
    uint32_t checksum;
    uint32_t routineId;
    uint8_t stepNum;
    ESPSystemCommandPayload payload;
};

struct __attribute__((packed)) ESPAddExitConditionToRoutineStepMessage {
    uint32_t checksum;
    uint32_t routineId;
    uint8_t stepNum;
    ESPRoutineStateExitConditionType exitCondition;
    float value;
    uint8_t exitToStepNum;
};

struct __attribute__((packed)) ESPESPStatusMessage {
    int64_t unixTimestamp;
    bool pressureDeviceConnected;
    float pressureInBar;
    bool scaleConnected;
    float weightInGrams;
};
#endif //SMART_LCC_ESP_PROTOCOL_H
