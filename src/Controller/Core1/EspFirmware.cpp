//
// Created by Magnus Nordlander on 2023-01-04.
//

#include <cstdlib>
#include <cmath>
#include "EspFirmware.h"
#include "pico/time.h"
#include "hardware/regs/rosc.h"
#include "hardware/regs/addressmap.h"
#include "utils/crc32.h"
#include "utils/USBDebug.h"
#include "utils/hex_format.h"

jnk0le::Ringbuffer<uint8_t, 1024> EspFirmware::ringbuffer = {};
uart_inst_t* EspFirmware::interruptedUart = nullptr;

void EspFirmware::initInterrupts(uart_inst_t *uart) {
    EspFirmware::interruptedUart = uart;

    uart_set_fifo_enabled(uart, false);

    int UART_IRQ = uart == uart0 ? UART0_IRQ : UART1_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, EspFirmware::onUartRx);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(uart, true, false);
}

void EspFirmware::onUartRx() {
    if (EspFirmware::interruptedUart == nullptr) {
        return;
    }

    while (uart_is_readable(EspFirmware::interruptedUart)) {
        uint8_t ch = uart_getc(EspFirmware::interruptedUart);
        ringbuffer.insert( ch);
    }
}

bool EspFirmware::readFromRingBufferBlockingWithTimeout(uint8_t *dst, size_t len, absolute_time_t timeout_time) {
    timeout_state_t ts;
    check_timeout_fn timeout_check = init_single_timeout_until(&ts, timeout_time);

    for (size_t i = 0; i < len; ++i) {
        while (ringbuffer.readAvailable() < len) {
            if (timeout_check(&ts)) {
                return false;
            }

            tight_loop_contents();
        }
    }

    size_t readLen = ringbuffer.readBuff(dst, len);

    return readLen == len;
}

EspFirmware::EspFirmware(uart_inst_t *uart, PicoQueue<SystemControllerCommand> *commandQueue, SystemStatus* status, SettingsManager* settingsManager, Automations* automations) : uart(uart), commandQueue(commandQueue), status(status), settingsManager(settingsManager), automations(automations) {}

uint32_t rnd(void){
    int k, random=0;
    volatile uint32_t *rnd_reg=(uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);

    for(k=0;k<32;k++){

        random = random << 1;
        random=random + (0x00000001 & (*rnd_reg));

    }
    return random;
}


bool EspFirmware::pingBlocking() {
    ESPMessageHeader pingHeader{
            .direction = ESP_DIRECTION_RP2040_TO_ESP32,
            .id = 1,
            .responseTo = 0,
            .type = ESP_MESSAGE_PING,
            .error = ESP_ERROR_NONE,
            .version = ESP_RP2040_PROTOCOL_VERSION,
            .length = sizeof(ESPPingMessage),
    };

    ESPPingMessage pingMessage{};

    ringbuffer.consumerClear();

    uart_write_blocking(uart, reinterpret_cast<const uint8_t *>(&pingHeader), sizeof(pingHeader));
    uart_write_blocking(uart, reinterpret_cast<const uint8_t *>(&pingMessage), sizeof(pingMessage));

    ESPMessageHeader replyHeader{};

    bool success = readFromRingBufferBlockingWithTimeout(reinterpret_cast<uint8_t *>(&replyHeader), sizeof(ESPMessageHeader), make_timeout_time_ms(100));

    if (success && replyHeader.length > 0) {
        auto *response = static_cast<uint8_t *>(malloc(replyHeader.length));
        success = readFromRingBufferBlockingWithTimeout(response, replyHeader.length, make_timeout_time_ms(50));

        if (success) {
            if (replyHeader.type != ESP_MESSAGE_PONG || replyHeader.responseTo != 1 || replyHeader.error != ESP_ERROR_NONE) {
                free(response);

                return false;
            } else {
                auto *replyMessage = reinterpret_cast<ESPPongMessage *>(response);
                if (replyMessage->version != 0x0001) {
                    free(response);
                    return false;
                }
            }
        }

        free(response);
        return true;
    }

    return false;
}

bool EspFirmware::sendStatus(
        SystemControllerStatusMessage *systemControllerStatusMessage,
        float externalTemperature1,
        float externalTemperature2,
        float externalTemperature3,
        uint16_t autoSleepMinutes,
        float plannedSleepInSeconds,
        uint16_t currentRoutine,
        uint16_t currentRoutineStep
                ) {
    ESPMessageHeader statusHeader{
            .direction = ESP_DIRECTION_RP2040_TO_ESP32,
            .id = rnd(),
            .responseTo = 0,
            .type = ESP_MESSAGE_SYSTEM_STATUS,
            .error = ESP_ERROR_NONE,
            .version = ESP_RP2040_PROTOCOL_VERSION,
            .length = sizeof(ESPSystemStatusMessage),
    };

    uint16_t autosleepIn = 0;
    if (!std::isinf(plannedSleepInSeconds)) {
        autosleepIn = (uint16_t)plannedSleepInSeconds;
    }

    uint8_t flowMode = ESP_FLOW_MODE_FULL_FLOW;
    switch (systemControllerStatusMessage->flowMode) {
        case FULL_FLOW:
            flowMode = ESP_FLOW_MODE_FULL_FLOW;
            break;
        case PUMP_ON_PWM_SOLENOID:
            flowMode = ESP_FLOW_MODE_PUMP_ON_PWM_SOLENOID;
            break;
        case PUMP_OFF_PWM_SOLENOID:
            flowMode = ESP_FLOW_MODE_PUMP_OFF_PWM_SOLENOID;
            break;
        case PUMP_OFF_SOLENOID_OPEN:
            flowMode = ESP_FLOW_MODE_PUMP_OFF_SOLENOID_OPEN;
            break;
    }

    ESPSystemStatusMessage statusMessage{
            .internalState = getInternalState(systemControllerStatusMessage->internalState),
            .runState = getRunState(systemControllerStatusMessage->runState),
            .coalescedState = getCoalescedState(systemControllerStatusMessage->coalescedState),
            .brewBoilerTemperature = systemControllerStatusMessage->offsetBrewTemperature,
            .brewBoilerSetPoint = systemControllerStatusMessage->offsetBrewSetPoint,
            .serviceBoilerTemperature = systemControllerStatusMessage->serviceTemperature,
            .serviceBoilerSetPoint = systemControllerStatusMessage->serviceSetPoint,
            .brewTemperatureOffset = systemControllerStatusMessage->brewTemperatureOffset,
            .autoSleepAfter = autoSleepMinutes,
            .currentlyBrewing = systemControllerStatusMessage->currentlyBrewing,
            .currentlyFillingServiceBoiler = systemControllerStatusMessage->currentlyFillingServiceBoiler,
            .ecoMode = systemControllerStatusMessage->ecoMode,
            .sleepMode = systemControllerStatusMessage->sleepMode,
            .waterTankLow = systemControllerStatusMessage->waterTankLow,
            .plannedAutoSleepInSeconds = autosleepIn,
            .rp2040Temperature = 0,
            .numBails = systemControllerStatusMessage->bailCounter,
            .rp2040UptimeSeconds = to_ms_since_boot(systemControllerStatusMessage->timestamp) / 1000,
            .sbRawHi = systemControllerStatusMessage->sbRawHi,
            .sbRawLo = systemControllerStatusMessage->sbRawLo,
            .externalTemperature1 = externalTemperature1,
            .externalTemperature2 = externalTemperature2,
            .externalTemperature3 = externalTemperature3,
            .flowMode = flowMode,
            .brewBoilerOn = systemControllerStatusMessage->brewSSRActive,
            .serviceBoilerOn = systemControllerStatusMessage->serviceSSRActive,
            .loadedRoutine = currentRoutine,
            .currentRoutineStep = currentRoutineStep,
    };

    ringbuffer.consumerClear();

    uart_write_blocking(uart, reinterpret_cast<const uint8_t *>(&statusHeader), sizeof(statusHeader));
    uart_write_blocking(uart, reinterpret_cast<const uint8_t *>(&statusMessage), sizeof(statusMessage));

    return waitForAck(statusHeader.id);
}

bool EspFirmware::waitForAck(uint32_t id) {
    ESPMessageHeader replyHeader{};

    bool success = readFromRingBufferBlockingWithTimeout(reinterpret_cast<uint8_t *>(&replyHeader), sizeof(ESPMessageHeader), make_timeout_time_ms(100));

    if (success) {
        if (replyHeader.length > 0) {
            auto *response = static_cast<uint8_t *>(malloc(replyHeader.length));
            success = readFromRingBufferBlockingWithTimeout(response, replyHeader.length, make_timeout_time_ms(50));

            free(response);

            // Acks are zero length
            return false;
        }

        return replyHeader.type == ESP_MESSAGE_ACK && replyHeader.responseTo == id && replyHeader.error == ESP_ERROR_NONE;
    }

    return false;
}

void EspFirmware::loop() {
    if (!ringbuffer.isEmpty()) {
        ESPMessageHeader header{};
        bool success = readFromRingBufferBlockingWithTimeout(reinterpret_cast<uint8_t *>(&header), sizeof(ESPMessageHeader), make_timeout_time_ms(10));

        if (success) {
            printf("Message received\n");
            printlnhex(reinterpret_cast<uint8_t *>(&header), sizeof(ESPMessageHeader));
            if (header.direction == ESP_DIRECTION_ESP32_TO_RP2040) {
                switch(header.type) {
                    case ESP_MESSAGE_SYSTEM_COMMAND:
                        return handleCommand(&header);
//                    case ESP_MESSAGE_ESP_STATUS:
//                        return handleESPStatus(&header);
                    case ESP_MESSAGE_PING:
                    case ESP_MESSAGE_PONG:
                    case ESP_MESSAGE_ACK:
                    case ESP_MESSAGE_NACK:
                    case ESP_MESSAGE_SYSTEM_STATUS:
                    default:
                        ringbuffer.consumerClear();
                        return;

                }
            }
        }

        ringbuffer.consumerClear();
    }
}

void EspFirmware::handleESPStatus(ESPMessageHeader *header) {
    if (header->length != sizeof(ESPESPStatusMessage)) {
        ringbuffer.consumerClear();
        return sendNack(header->id, ESP_ERROR_UNEXPECTED_MESSAGE_LENGTH);
    }

    ESPESPStatusMessage message{};
    bool success = readFromRingBufferBlockingWithTimeout(reinterpret_cast<uint8_t *>(&message), sizeof(ESPESPStatusMessage), make_timeout_time_ms(50));

    if (!success) {
        return sendNack(header->id, ESP_ERROR_INCOMPLETE_DATA);
    }

    status->updateEspStatusMessage(message);

    sendAck(header->id);
}

void EspFirmware::handleCommand(ESPMessageHeader *header) {
    printf("Command received");
    if (header->length == sizeof(ESPSystemCommandMessage)) {
        ESPSystemCommandMessage message{};
        bool success = readFromRingBufferBlockingWithTimeout(reinterpret_cast<uint8_t *>(&message), sizeof(ESPSystemCommandMessage), make_timeout_time_ms(50));

        if (success) {
            crc32_t crc;
            crc32(&message.payload, sizeof(ESPSystemCommandPayload), &crc);

            if (crc == message.checksum) {
                switch (message.payload.type) {
                    case ESP_SYSTEM_COMMAND_SET_SLEEP_MODE:
                        if (!message.payload.bool1) {
                            automations->exitingSleep();
                        }
                        settingsManager->setSleepMode(message.payload.bool1);
                        break;
                    case ESP_SYSTEM_COMMAND_SET_BREW_SET_POINT:
                        settingsManager->setOffsetTargetBrewTemp(message.payload.float1);
                        break;
                    case ESP_SYSTEM_COMMAND_SET_BREW_PID_PARAMETERS:
                        settingsManager->setBrewPidParameters(PidSettings{
                                .Kp = message.payload.float1,
                                .Ki = message.payload.float2,
                                .Kd = message.payload.float3,
                                .windupLow = message.payload.float4,
                                .windupHigh = message.payload.float5
                        });
                        break;
                    case ESP_SYSTEM_COMMAND_SET_BREW_OFFSET:
                        settingsManager->setBrewTemperatureOffset(message.payload.float1);
                        break;
                    case ESP_SYSTEM_COMMAND_SET_SERVICE_SET_POINT:
                        settingsManager->setTargetServiceTemp(message.payload.float1);
                        break;
                    case ESP_SYSTEM_COMMAND_SET_SERVICE_PID_PARAMETERS:
                        settingsManager->setServicePidParameters(PidSettings{
                                .Kp = message.payload.float1,
                                .Ki = message.payload.float2,
                                .Kd = message.payload.float3,
                                .windupLow = message.payload.float4,
                                .windupHigh = message.payload.float5
                        });
                        break;
                    case ESP_SYSTEM_COMMAND_SET_ECO_MODE:
                        settingsManager->setEcoMode(message.payload.bool1);
                        break;
                    case ESP_SYSTEM_COMMAND_SET_AUTO_SLEEP_MINUTES:
                        settingsManager->setAutoSleepMin(message.payload.float1);
                        break;
                    case ESP_SYSTEM_COMMAND_FORCE_HARD_BAIL: {
                        auto command = SystemControllerCommand{.type = COMMAND_FORCE_HARD_BAIL};
                        commandQueue->addBlocking(&command);
                        break;
                    }
                    case ESP_SYSTEM_COMMAND_SET_FLOW_MODE: {
                        uint32_t arg;
                        switch (message.payload.int1) {
                            case ESP_FLOW_MODE_FULL_FLOW:
                                arg = FULL_FLOW;
                                break;
                            case ESP_FLOW_MODE_PUMP_OFF_SOLENOID_OPEN:
                                arg = PUMP_OFF_SOLENOID_OPEN;
                                break;
                            case ESP_FLOW_MODE_PUMP_OFF_PWM_SOLENOID:
                                arg = PUMP_OFF_PWM_SOLENOID;
                                break;
                            case ESP_FLOW_MODE_PUMP_ON_PWM_SOLENOID:
                                arg = PUMP_ON_PWM_SOLENOID;
                                break;
                            default:
                                arg = FULL_FLOW;
                        }

                        auto command = SystemControllerCommand{.type = COMMAND_SET_FLOW_MODE, .int1 = arg};
                        commandQueue->addBlocking(&command);
                        break;
                    }
                    case ESP_SYSTEM_COMMAND_ENQUEUE_ROUTINE:
                        automations->enqueueRoutine(message.payload.int1);
                        break;
                    case ESP_SYSTEM_COMMAND_CLEAR_ROUTINE:
                        automations->cancelRoutine();
                }

                sendAck(header->id);
            } else {
                sendNack(header->id, ESP_ERROR_INVALID_CHECKSUM);
            }
        } else {
            sendNack(header->id, ESP_ERROR_INCOMPLETE_DATA);
        }
    } else {
        //USB_PRINTF("Unexpected message length. Expected: %u Received: %lu\n", sizeof(ESPSystemCommandMessage), header->length);
        sendNack(header->id, ESP_ERROR_UNEXPECTED_MESSAGE_LENGTH);
    }
}

void EspFirmware::sendAck(uint32_t messageId) {
    ESPMessageHeader ackHeader{
            .direction = ESP_DIRECTION_RP2040_TO_ESP32,
            .id = rnd(),
            .responseTo = messageId,
            .type = ESP_MESSAGE_ACK,
            .error = ESP_ERROR_NONE,
            .version = ESP_RP2040_PROTOCOL_VERSION,
            .length = 0,
    };

    uart_write_blocking(uart, reinterpret_cast<const uint8_t *>(&ackHeader), sizeof(ackHeader));
}

void EspFirmware::sendNack(uint32_t messageId, ESPError error) {
    ESPMessageHeader ackHeader{
            .direction = ESP_DIRECTION_RP2040_TO_ESP32,
            .id = rnd(),
            .responseTo = messageId,
            .type = ESP_MESSAGE_NACK,
            .error = error,
            .version = ESP_RP2040_PROTOCOL_VERSION,
            .length = 0,
    };

    uart_write_blocking(uart, reinterpret_cast<const uint8_t *>(&ackHeader), sizeof(ackHeader));
}
