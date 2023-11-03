//
// Created by Magnus Nordlander on 2023-10-16.
//

#include <cstdio>
#include "MCP9600.h"
#include "utils/USBDebug.h"

#define DEV_RESOLUTION 0.0625

void MCP9600::initialize() {
    writeRegisterByte(MCP9600_REGISTER_THERMO_SENSOR_CONFIG, probeType_);
//    writeRegisterByte(MCP9600_REGISTER_DEVICE_CONFIG, 0xAE); // Burst mode
    writeRegisterByte(MCP9600_REGISTER_DEVICE_CONFIG, 0xAC); // Normal mode
}

float MCP9600::readTemperature(unsigned char status_mode) {
    if (!statusCheck(status_mode)) {
        return lastReadHotJunctionTemperature;
    }

    int16_t raw = readDoubleRegister(MCP9600_REGISTER_HOT_JUNC_TEMP);

    uint8_t status = readRegisterByte(MCP9600_REGISTER_SENSOR_STATUS);
    status &= 0xBF;
    //USB_PRINTF("Writing Sensor Status: %x\n", status);
    writeRegisterByte(MCP9600_REGISTER_SENSOR_STATUS, status);

    lastReadHotJunctionTemperature = ((float) raw * DEV_RESOLUTION);

    return lastReadHotJunctionTemperature;
}

bool MCP9600::writeRegisterByte(MCP9600Register mcp9600Register, uint8_t data) {
    uint8_t message[2] = {mcp9600Register, data};

    return i2c_write_blocking(i2c_, addr_, message, sizeof(message), false) == 2;
}

uint8_t MCP9600::readRegisterByte(MCP9600Register reg) {
    i2c_write_blocking(i2c_, addr_, reinterpret_cast<const uint8_t *>(&reg), sizeof(reg), false);

    uint8_t byte;

    i2c_read_blocking(i2c_, addr_, &byte, sizeof(byte), false);

    return byte;
}

void MCP9600::readRegisterBuf(MCP9600Register reg, uint8_t* buf, size_t size) {
    i2c_write_blocking(i2c_, addr_, reinterpret_cast<const uint8_t *>(&reg), sizeof(reg), false);

    i2c_read_blocking(i2c_, addr_, buf, size, false);
}


bool MCP9600::statusCheck(uint8_t status) {
    uint8_t statReg = readRegisterByte(MCP9600_REGISTER_SENSOR_STATUS);

    //USB_PRINTF("Sensor Status: %x\n", statReg);

    return (statReg & status) == status;
}

bool MCP9600::isConnected() {
    if (!isPresent) {
        uint8_t rxdata;
        auto ret = i2c_read_blocking(i2c_, addr_, &rxdata, 1, false);
        isPresent = ret > 0;
    }

    return isPresent;
}

uint16_t MCP9600::readDoubleRegister(MCP9600Register reg) {
    //Attempt to read the register until we exit with no error code
    //This attempts to fix the bug where clock stretching sometimes failes, as
    //described in the MCP9600 eratta
    for (uint8_t attempts = 0; attempts <= 3; attempts++)
    {
        i2c_write_blocking(i2c_, addr_, reinterpret_cast<const uint8_t *>(&reg), sizeof(reg), false);

        uint8_t bytes[2];
        i2c_read_blocking(i2c_, addr_, bytes, 2, false);

        uint16_t data = bytes[0] << 8;
        data |= bytes[1];
        return data;
    }
    return (0);
}
