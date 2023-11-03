//
// Created by Magnus Nordlander on 2021-06-27.
//

#ifndef LCC_RELAY_LCC_PROTOCOL_H
#define LCC_RELAY_LCC_PROTOCOL_H

#include <cstdint>
#include <stdint-gcc.h>

typedef enum : uint16_t {
    LCC_VALIDATION_ERROR_NONE = 0,
    LCC_VALIDATION_ERROR_INVALID_HEADER = 1 << 1,
    LCC_VALIDATION_ERROR_INVALID_CHECKSUM = 1 << 2,
    LCC_VALIDATION_ERROR_UNEXPECTED_FLAGS = 1 << 3,
    LCC_VALIDATION_ERROR_BOTH_SSRS_ON = 1 << 4,
    LCC_VALIDATION_ERROR_SOLENOID_OPEN_WITHOUT_PUMP = 1 << 5,
} LccPacketValidationError;

typedef enum : uint8_t {
    LCC_SHIFT_REGISTER_1_CN6_1 = 1 << 0,
    LCC_SHIFT_REGISTER_1_CN6_3 = 1 << 1,
    LCC_SHIFT_REGISTER_1_CN6_5 = 1 << 2, // On V3: Status LED
    LCC_SHIFT_REGISTER_1_BREW_BOILER_RELAY = 1 << 3,
    LCC_SHIFT_REGISTER_1_FA7_SOLENOID = 1 << 4, // Solenoid that lets water into the service boiler
    LCC_SHIFT_REGISTER_1_FA8_SOLENOID = 1 << 5, // On V2: Unpopulated, On V3: Solenoid that lets water into either the brew boiler or the service boiler
    LCC_SHIFT_REGISTER_1_DISABLE_OLED_12V = 1 << 6, // Unknown if this actually does anything
    LCC_SHIFT_REGISTER_1_DISABLE_OLED_3V3 = 1 << 7, // Unknown if this actually does anything
} LccShiftRegister1Flags;

typedef enum : uint8_t {
    LCC_SHIFT_REGISTER_2_SERVICE_BOILER_RELAY = 1 << 0,
    LCC_SHIFT_REGISTER_2_PUMP_RELAY = 1 << 4,
    LCC_SHIFT_REGISTER_2_FA9 = 1 << 5, // Unpopulated on at least the V2 control board
} LccShiftRegister2Flags;

struct LccRawPacket {
    uint8_t header{};
    uint8_t shiftRegister2{};
    uint8_t shiftRegister1{};
    uint8_t byte3{};
    uint8_t checksum{};
};

struct LccParsedPacket {
    bool pump_on = false;
    bool water_line_solenoid_open = false;
    bool service_boiler_ssr_on = false;
    bool service_boiler_solenoid_open = false;
    bool brew_boiler_ssr_on = false;
    bool minus_button_pressed = false;
    bool plus_button_pressed = false;
};

LccRawPacket convert_lcc_parsed_to_raw(LccParsedPacket parsed);
LccParsedPacket convert_lcc_raw_to_parsed(LccRawPacket raw);
LccRawPacket create_safe_packet();
uint16_t validate_lcc_raw_packet(LccRawPacket raw);

#endif //LCC_RELAY_LCC_PROTOCOL_H
