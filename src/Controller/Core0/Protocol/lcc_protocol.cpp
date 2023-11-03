//
// Created by Magnus Nordlander on 2021-06-27.
//

#include "lcc_protocol.h"
#include "utils/checksum.h"

LccRawPacket convert_lcc_parsed_to_raw(LccParsedPacket parsed) {
    LccRawPacket packet = LccRawPacket();

    packet.header = 0x80;
    packet.shiftRegister2 = (parsed.pump_on ? LCC_SHIFT_REGISTER_2_PUMP_RELAY : 0x00) | (parsed.service_boiler_ssr_on ? LCC_SHIFT_REGISTER_2_SERVICE_BOILER_RELAY : 0x00);
    packet.shiftRegister1 = (parsed.water_line_solenoid_open ? LCC_SHIFT_REGISTER_1_FA8_SOLENOID : 0x00) | (parsed.service_boiler_solenoid_open ? LCC_SHIFT_REGISTER_1_FA7_SOLENOID : 0x00) | (parsed.brew_boiler_ssr_on ? LCC_SHIFT_REGISTER_1_BREW_BOILER_RELAY : 0x00);
    packet.byte3 = (parsed.minus_button_pressed ? 0x08 : 0x00) | (parsed.plus_button_pressed ? 0x04 : 0x00);
    static_assert(sizeof(packet) > 2, "LCC Packet is too small, for some reason");
    packet.checksum = calculate_checksum(((uint8_t *) &packet + 1), sizeof(packet) - 2, 0x00);

    return packet;
}

LccParsedPacket convert_lcc_raw_to_parsed(LccRawPacket raw) {
    LccParsedPacket parsed = LccParsedPacket();

    parsed.pump_on = raw.shiftRegister2 & LCC_SHIFT_REGISTER_2_PUMP_RELAY;
    parsed.water_line_solenoid_open = raw.shiftRegister1 & LCC_SHIFT_REGISTER_1_FA8_SOLENOID;
    parsed.service_boiler_ssr_on = raw.shiftRegister2 & LCC_SHIFT_REGISTER_2_SERVICE_BOILER_RELAY;
    parsed.service_boiler_solenoid_open = raw.shiftRegister1 & LCC_SHIFT_REGISTER_1_FA7_SOLENOID;
    parsed.brew_boiler_ssr_on = raw.shiftRegister1 & LCC_SHIFT_REGISTER_1_BREW_BOILER_RELAY;
    parsed.minus_button_pressed = raw.byte3 & 0x08;
    parsed.plus_button_pressed = raw.byte3 & 0x04;

    return parsed;
}

LccRawPacket create_safe_packet() {
    return (LccRawPacket){0x80, 0, 0, 0, 0};
}

uint16_t validate_lcc_raw_packet(LccRawPacket packet) {
    uint16_t error = LCC_VALIDATION_ERROR_NONE;

    if (packet.header != 0x80) {
        error |= LCC_VALIDATION_ERROR_INVALID_HEADER;
    }

    static_assert(sizeof(packet) == 5, "Weird LCC Packet size");
    uint8_t calculated_checksum = calculate_checksum(((uint8_t *) &packet + 1), sizeof(packet) - 2, 0x00);
    if (calculated_checksum != packet.checksum) {
        error |= LCC_VALIDATION_ERROR_INVALID_CHECKSUM;
    }

    if (packet.shiftRegister2 & 0xEE || packet.shiftRegister1 & 0xC7 || packet.byte3 & 0xF3) {
        error |= LCC_VALIDATION_ERROR_UNEXPECTED_FLAGS;
    }

    if (packet.shiftRegister2 & 0x01 && packet.shiftRegister1 & 0x08) {
        error |= LCC_VALIDATION_ERROR_BOTH_SSRS_ON;
    }

    if (packet.shiftRegister1 & 0x10 && (packet.shiftRegister2 & 0x10) == 0x00) {
        error |= LCC_VALIDATION_ERROR_SOLENOID_OPEN_WITHOUT_PUMP;
    }

    return error;
}
