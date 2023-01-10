//
// Created by Magnus Nordlander on 2023-01-01.
//

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"

#include <cstdint>
#include <cmath>
#include "esp-protocol.h"
#include "LambdaSwitch.h"
#include "LambdaNumber.h"

uint32_t esp_random();

class LCCUart : public esphome::PollingComponent, public esphome::uart::UARTDevice {
public:
    esphome::sensor::Sensor *brewTemperatureSensor{nullptr};
    esphome::sensor::Sensor *serviceTemperatureSensor{nullptr};
    esphome::text_sensor::TextSensor *coalescedStateSensor{nullptr};
    LambdaSwitch *ecoModeSwitch{nullptr};
    LambdaSwitch *sleepSwitch{nullptr};
    LambdaNumber *brewTemperatureSetPoint{nullptr};
    LambdaNumber *serviceTemperatureSetPoint{nullptr};
    LambdaNumber *brewTemperatureOffset{nullptr};
    esphome::binary_sensor::BinarySensor *waterTankLowSensor{nullptr};
    esphome::binary_sensor::BinarySensor *currentlyBrewingSensor{nullptr};
    esphome::binary_sensor::BinarySensor *currentlyFillingServiceBoilerSensor{nullptr};

    esphome::wifi::WiFiComponent *wifiComponent;

    explicit LCCUart(esphome::uart::UARTComponent *parent, esphome::wifi::WiFiComponent *wifiComponent) : PollingComponent(10000), UARTDevice(parent), wifiComponent(wifiComponent) {
        brewTemperatureSensor = new esphome::sensor::Sensor();
        brewTemperatureSensor->set_name("Brew temperature");
        brewTemperatureSensor->set_disabled_by_default(false);
        brewTemperatureSensor->set_unit_of_measurement("\302\260C");
        brewTemperatureSensor->set_accuracy_decimals(1);
        brewTemperatureSensor->set_force_update(false);

        serviceTemperatureSensor = new esphome::sensor::Sensor();
        serviceTemperatureSensor->set_name("Service temperature");
        serviceTemperatureSensor->set_disabled_by_default(false);
        serviceTemperatureSensor->set_unit_of_measurement("\302\260C");
        serviceTemperatureSensor->set_accuracy_decimals(1);
        serviceTemperatureSensor->set_force_update(false);

        coalescedStateSensor = new esphome::text_sensor::TextSensor();
        coalescedStateSensor->set_name("State");
        coalescedStateSensor->set_disabled_by_default(false);

        ecoModeSwitch = new LambdaSwitch([=](bool state) {
            sendCommand(ESP_SYSTEM_COMMAND_SET_ECO_MODE, state);
        });
        ecoModeSwitch->set_name("Eco mode");
        ecoModeSwitch->set_disabled_by_default(false);

        sleepSwitch = new LambdaSwitch([=](bool state) {
            sendCommand(ESP_SYSTEM_COMMAND_SET_SLEEP_MODE, state);
        });
        sleepSwitch->set_name("Sleep mode");
        sleepSwitch->set_disabled_by_default(false);

        brewTemperatureSetPoint = new LambdaNumber([=](float state) {
            sendCommand(ESP_SYSTEM_COMMAND_SET_BREW_SET_POINT, state);
        });
        brewTemperatureSetPoint->set_name("Brew temperature set point");
        brewTemperatureSetPoint->set_disabled_by_default(false);
        brewTemperatureSetPoint->traits.set_min_value(0);
        brewTemperatureSetPoint->traits.set_max_value(100);
        brewTemperatureSetPoint->traits.set_step(0.1f);
        brewTemperatureSetPoint->traits.set_unit_of_measurement("\302\260C");

        serviceTemperatureSetPoint = new LambdaNumber([=](float state) {
            sendCommand(ESP_SYSTEM_COMMAND_SET_SERVICE_SET_POINT, state);
        });
        serviceTemperatureSetPoint->set_name("Service temperature set point");
        serviceTemperatureSetPoint->set_disabled_by_default(false);
        serviceTemperatureSetPoint->traits.set_min_value(0);
        serviceTemperatureSetPoint->traits.set_max_value(130);
        serviceTemperatureSetPoint->traits.set_step(0.1f);
        serviceTemperatureSetPoint->traits.set_unit_of_measurement("\302\260C");

        brewTemperatureOffset = new LambdaNumber([=](float state) {
            sendCommand(ESP_SYSTEM_COMMAND_SET_BREW_OFFSET, state);
        });
        brewTemperatureOffset->set_name("Brew temperature offset");
        brewTemperatureOffset->set_disabled_by_default(true);
        brewTemperatureOffset->traits.set_min_value(-20);
        brewTemperatureOffset->traits.set_max_value(20);
        brewTemperatureOffset->traits.set_step(0.1f);
        brewTemperatureOffset->traits.set_unit_of_measurement("\302\260C");

        waterTankLowSensor = new esphome::binary_sensor::BinarySensor();
        waterTankLowSensor->set_name("Water tank low");
        waterTankLowSensor->set_disabled_by_default(false);

        currentlyBrewingSensor = new esphome::binary_sensor::BinarySensor();
        currentlyBrewingSensor->set_name("Currently Brewing");
        currentlyBrewingSensor->set_disabled_by_default(false);

        currentlyFillingServiceBoilerSensor = new esphome::binary_sensor::BinarySensor();
        currentlyFillingServiceBoilerSensor->set_name("Currently filling service boiler");
        currentlyFillingServiceBoilerSensor->set_disabled_by_default(true);
    }

    void update() override {
        ESP_LOGD("custom", "Sending status message to RP2040");
        if (wifiComponent->is_connected()) {
            ESP_LOGD("custom", "Wifi is connected");
        } else {
            ESP_LOGD("custom", "Wifi is not connected");
        }

        ESPMessageHeader header{
            .direction = ESP_DIRECTION_ESP32_TO_RP2040,
            .id = esp_random(),
            .responseTo = 0,
            .type = ESP_MESSAGE_ESP_STATUS,
            .error = ESP_ERROR_NONE,
            .length = sizeof(ESPESPStatusMessage)
        };

        ESPESPStatusMessage message{
            .wifiConnected = wifiComponent->is_connected(),
            .improvAuthorizationRequested = false,
        };

        this->write_array(reinterpret_cast<const uint8_t *>(&header), sizeof(ESPMessageHeader));
        this->write_array(reinterpret_cast<const uint8_t *>(&message), sizeof(ESPESPStatusMessage));
    }

    void setup() override {
        ESP_LOGD("custom", "Component booting up");
        // nothing to do here
    }

    void loop() override {
        if (this->available()) {
            ESPMessageHeader header{};
            bool success = this->read_array(reinterpret_cast<uint8_t *>(&header), sizeof(ESPMessageHeader));

            if (!success) {
                ESP_LOGW("custom", "There was stuff on the UART, but we didn't get a message");
                return;
            }

            switch (header.type) {
                case ESP_MESSAGE_PING:
                    return handlePingMessage(&header);
                case ESP_MESSAGE_SYSTEM_STATUS:
                    return handleSystemStatusMessage(&header);
                case ESP_MESSAGE_NACK:
                    return handleNack(&header);
                case ESP_MESSAGE_PONG:
                case ESP_MESSAGE_ACK:
                case ESP_MESSAGE_SYSTEM_COMMAND:
                    ESP_LOGD("custom", "Non-ping message");
                    break;
            }
        }
    }

    void handleNack(ESPMessageHeader* header) {
        ESP_LOGE("custom", "NACK message, error: %u", header->error);
    }

    void handlePingMessage(ESPMessageHeader* header) {
        ESP_LOGD("custom", "Ping message");
        if (header->length == sizeof(ESPPingMessage)) {
            ESPPingMessage message{};
            bool success = this->read_array(reinterpret_cast<uint8_t *>(&message), sizeof(message));

            if (success) {
                if (message.version == 0x0001) {
                    ESP_LOGD("custom", "Sending Pong");
                    ESPMessageHeader responseHeader{
                            .direction = ESP_DIRECTION_ESP32_TO_RP2040,
                            .id = static_cast<uint16_t>(esp_random()),
                            .responseTo = header->id,
                            .type = ESP_MESSAGE_PONG,
                            .error = ESP_ERROR_NONE,
                            .length = sizeof(ESPPongMessage),
                    };
                    ESPPongMessage responseMessage{};

                    this->write_array(reinterpret_cast<const uint8_t *>(&responseHeader), sizeof(ESPMessageHeader));
                    this->write_array(reinterpret_cast<const uint8_t *>(&responseMessage), sizeof(ESPPongMessage));
                } else {
                    ESP_LOGW("custom", "Incorrect message, sending error");
                    ESPMessageHeader responseHeader{
                            .direction = ESP_DIRECTION_ESP32_TO_RP2040,
                            .id = static_cast<uint16_t>(esp_random()),
                            .responseTo = header->id,
                            .type = ESP_MESSAGE_NACK,
                            .error = ESP_ERROR_PING_WRONG_VERSION,
                            .length = 0,
                    };

                    this->write_array(reinterpret_cast<const uint8_t *>(&responseHeader), sizeof(ESPMessageHeader));
                }
            } else {
                ESP_LOGD("custom", "Didn't get complete data");
                nackMessage(header);
            }
        }
    }

    void handleSystemStatusMessage(ESPMessageHeader* header) {
        ESP_LOGD("custom", "System status message");
        if (header->length == sizeof(ESPSystemStatusMessage)) {
            ESPSystemStatusMessage message{};
            bool success = this->read_array(reinterpret_cast<uint8_t *>(&message), sizeof(message));

            if (success) {
                ackMessage(header);

                float brewTemp = message.brewBoilerTemperature;
                float serviceTemp = message.serviceBoilerTemperature;

                if (!brewTemperatureSensor->has_state() || fabs(brewTemp - brewTemperatureSensor->state) > 0.05) {
                    brewTemperatureSensor->publish_state(brewTemp);
                }

                if (!serviceTemperatureSensor->has_state() || fabs(serviceTemp - serviceTemperatureSensor->state) > 0.05) {
                    serviceTemperatureSensor->publish_state(serviceTemp);
                }

                std::string coalescedStateString = prettifyCoalescedStateString(message.coalescedState);

                if (!coalescedStateSensor->has_state() || coalescedStateSensor->state != coalescedStateString) {
                    coalescedStateSensor->publish_state(coalescedStateString);
                }

                float brewSetPoint = message.brewBoilerSetPoint;
                float serviceSetPoint = message.serviceBoilerSetPoint;
                float tempOffset = message.brewTemperatureOffset;

                if (!brewTemperatureSetPoint->has_state() || fabs(brewSetPoint - brewTemperatureSetPoint->state) > 0.05) {
                    brewTemperatureSetPoint->publish_state(brewSetPoint);
                }

                if (!serviceTemperatureSetPoint->has_state() || fabs(serviceSetPoint - serviceTemperatureSetPoint->state) > 0.05) {
                    serviceTemperatureSetPoint->publish_state(serviceSetPoint);
                }

                if (!brewTemperatureOffset->has_state() || fabs(tempOffset - brewTemperatureOffset->state) > 0.05) {
                    brewTemperatureOffset->publish_state(tempOffset);
                }

                if (!waterTankLowSensor->has_state() || message.waterTankLow != waterTankLowSensor->state) {
                    waterTankLowSensor->publish_state(message.waterTankLow);
                }

                if (!currentlyBrewingSensor->has_state() || message.currentlyBrewing != currentlyBrewingSensor->state) {
                    currentlyBrewingSensor->publish_state(message.currentlyBrewing);
                }

                if (!currentlyFillingServiceBoilerSensor->has_state() || message.currentlyFillingServiceBoiler != currentlyFillingServiceBoilerSensor->state) {
                    currentlyFillingServiceBoilerSensor->publish_state(message.currentlyFillingServiceBoiler);
                }

                if (message.ecoMode != ecoModeSwitch->state) {
                    ecoModeSwitch->publish_state(message.ecoMode);
                }

                if (message.sleepMode != sleepSwitch->state) {
                    sleepSwitch->publish_state(message.sleepMode);
                }
            } else {
                ESP_LOGW("custom", "Didn't get complete data");

                nackMessage(header);
            }
        }
    }

    void ackMessage(ESPMessageHeader *header)
    {
        ESPMessageHeader responseHeader{
                .direction = ESP_DIRECTION_ESP32_TO_RP2040,
                .id = static_cast<uint16_t>(esp_random()),
                .responseTo = header->id,
                .type = ESP_MESSAGE_ACK,
                .error = ESP_ERROR_NONE,
                .length = 0,
        };

        this->write_array(reinterpret_cast<const uint8_t *>(&responseHeader), sizeof(ESPMessageHeader));
    }

    void nackMessage(ESPMessageHeader *header)
    {
        ESPMessageHeader responseHeader{
                .direction = ESP_DIRECTION_ESP32_TO_RP2040,
                .id = esp_random(),
                .responseTo = header->id,
                .type = ESP_MESSAGE_NACK,
                .error = ESP_ERROR_UNEXPECTED_MESSAGE_LENGTH,
                .length = 0,
        };

        this->write_array(reinterpret_cast<const uint8_t *>(&responseHeader), sizeof(ESPMessageHeader));
    }

    void sendCommand(ESPSystemCommandType type, bool boolArg)
    {
        ESPSystemCommandPayload payload{
            .type = type,
            .bool1 = boolArg,
            .float1 = 0.0f,
            .float2 = 0.0f,
            .float3 = 0.0f,
            .float4 = 0.0f,
            .float5 = 0.0f,
        };

        sendCommand(payload);
    }

    void sendCommand(ESPSystemCommandType type, float floatArg)
    {
        ESPSystemCommandPayload payload{
                .type = type,
                .bool1 = false,
                .float1 = floatArg,
                .float2 = 0.0f,
                .float3 = 0.0f,
                .float4 = 0.0f,
                .float5 = 0.0f,
        };

        sendCommand(payload);
    }


    void sendCommand(ESPSystemCommandPayload payload)
    {
        ESPMessageHeader commandHeader{
                .direction = ESP_DIRECTION_ESP32_TO_RP2040,
                .id = esp_random(),
                .responseTo = 0,
                .type = ESP_MESSAGE_SYSTEM_COMMAND,
                .error = ESP_ERROR_NONE,
                .length = sizeof(ESPSystemCommandMessage),
        };

        ESPSystemCommandMessage commandMessage{
            .checksum = crc32(&payload, sizeof(ESPSystemCommandPayload)),
            .payload = payload,
        };

        this->write_array(reinterpret_cast<const uint8_t *>(&commandHeader), sizeof(ESPMessageHeader));
        this->write_array(reinterpret_cast<const uint8_t *>(&commandMessage), sizeof(ESPSystemCommandMessage));
    }

    uint32_t crc32_for_byte(uint32_t r) {
        for(int j = 0; j < 8; ++j)
            r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
        return r ^ (uint32_t)0xFF000000L;
    }

    uint32_t crc32(const void *data, size_t n_bytes) {
        uint32_t crc = 0;
        static uint32_t table[0x100];
        if(!*table)
            for(size_t i = 0; i < 0x100; ++i)
                table[i] = crc32_for_byte(i);
        for(size_t i = 0; i < n_bytes; ++i)
            crc = table[(uint8_t)crc ^ ((uint8_t*)data)[i]] ^ crc >> 8;

        return crc;
    }

    std::string prettifyCoalescedStateString(ESPSystemCoalescedState state) {
        switch (state) {
            case ESP_SYSTEM_COALESCED_STATE_UNDETERMINED:
                return "Undetermined";
            case ESP_SYSTEM_COALESCED_STATE_HEATUP:
                return "Heatup";
            case ESP_SYSTEM_COALESCED_STATE_TEMPS_NORMALIZING:
                return "Temperatures normalizing";
            case ESP_SYSTEM_COALESCED_STATE_WARM:
                return "Warm";
            case ESP_SYSTEM_COALESCED_STATE_SLEEPING:
                return "Sleeping";
            case ESP_SYSTEM_COALESCED_STATE_BAILED:
                return "Bailed";
            case ESP_SYSTEM_COALESCED_STATE_FIRST_RUN:
                return "First run";
        }

        return "Unknown";
    }
};