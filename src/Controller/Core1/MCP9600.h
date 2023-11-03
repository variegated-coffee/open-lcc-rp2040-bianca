//
// Created by Magnus Nordlander on 2023-10-16.
//

#ifndef SMART_LCC_MCP9600_H
#define SMART_LCC_MCP9600_H

#include <hardware/i2c.h>

// register pointers for various device functions
enum MCP9600Register: uint8_t {
    MCP9600_REGISTER_HOT_JUNC_TEMP = 0x00,
    MCP9600_REGISTER_DELTA_JUNC_TEMP = 0x01,
    MCP9600_REGISTER_COLD_JUNC_TEMP = 0x02,
    MCP9600_REGISTER_RAW_ADC = 0x03,
    MCP9600_REGISTER_SENSOR_STATUS = 0x04,
    MCP9600_REGISTER_THERMO_SENSOR_CONFIG = 0x05,
    MCP9600_REGISTER_DEVICE_CONFIG = 0x06,
    MCP9600_REGISTER_ALERT1_CONFIG = 0x08,
    MCP9600_REGISTER_ALERT2_CONFIG = 0x09,
    MCP9600_REGISTER_ALERT3_CONFIG = 0x0A,
    MCP9600_REGISTER_ALERT4_CONFIG = 0x0B,
    MCP9600_REGISTER_ALERT1_HYSTERESIS = 0x0C,
    MCP9600_REGISTER_ALERT2_HYSTERESIS = 0x0D,
    MCP9600_REGISTER_ALERT3_HYSTERESIS = 0x0E,
    MCP9600_REGISTER_ALERT4_HYSTERESIS = 0x0F,
    MCP9600_REGISTER_ALERT1_LIMIT = 0x10,
    MCP9600_REGISTER_ALERT2_LIMIT = 0x11,
    MCP9600_REGISTER_ALERT3_LIMIT = 0x12,
    MCP9600_REGISTER_ALERT4_LIMIT = 0x13,
    MCP9600_REGISTER_DEVICE_ID = 0x20,
};

enum MCP9600ProbeType: uint8_t
{
    MCP9600_PROBE_TYPE_K = 0x04,
    MCP9600_PROBE_TYPE_J = 0x14,
    MCP9600_PROBE_TYPE_T = 0x24,
    MCP9600_PROBE_TYPE_N = 0x34,
    MCP9600_PROBE_TYPE_S = 0x44,
    MCP9600_PROBE_TYPE_E = 0x54,
    MCP9600_PROBE_TYPE_B = 0x64,
    MCP9600_PROBE_TYPE_R = 0X74,
};

class MCP9600 {
public:
    explicit MCP9600(i2c_inst_t *i2c, uint8_t addr, MCP9600ProbeType probeType): i2c_(i2c), addr_(addr), probeType_(probeType) {

    }

    float readTemperature(unsigned char status_mode);
    void initialize();

    bool isConnected();

private:
    bool statusCheck(uint8_t status);

    bool writeRegisterByte(MCP9600Register reg, uint8_t data);
    uint8_t readRegisterByte(MCP9600Register reg);
    uint16_t readDoubleRegister(MCP9600Register reg);
    void readRegisterBuf(MCP9600Register reg, uint8_t *buf, size_t size);

    i2c_inst_t *i2c_{nullptr};
    uint8_t addr_;
    MCP9600ProbeType probeType_;

    bool isPresent = false;

    float lastReadHotJunctionTemperature = 0.f;
};


#endif //SMART_LCC_MCP9600_H
