cmake_minimum_required(VERSION 3.19)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-mcpu=cortex-m0plus -mthumb -Wswitch-enum -Wall -Wextra -DHARDWARE_REVISION_OPENLCC_R2A")
set(CMAKE_CXX_FLAGS_DEBUG "-g2 -O0 -ggdb2")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(CMAKE_C_FLAGS "-mcpu=cortex-m0plus -mthumb -Wswitch-enum -Wall -Wextra -DHARDWARE_REVISION_OPENLCC_R2A")
set(CMAKE_C_FLAGS_DEBUG "-g2 -O0 -ggdb2")
set(CMAKE_C_FLAGS_RELEASE "-O3")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initialize pico-sdk from GIT
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_FETCH_FROM_GIT on)

# pico_sdk_import.cmake is a single file copied from this SDK
# note: this must happen before project()
include(pico_sdk_import.cmake)

set(PICO_BOARD "arduino_nano_rp2040_connect")
set(PICO_CXX_ENABLE_EXCEPTIONS 1)

project(rp2040-bianca)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(bootloader lib/rp2040-serial-bootloader/main.c)

function(target_cl_options option)
    target_compile_options(bootloader PRIVATE ${option})
    target_link_options(bootloader PRIVATE ${option})
endfunction()

target_cl_options("-Os")
target_cl_options("-ffunction-sections")
target_cl_options("-fdata-sections")
target_link_options(bootloader PRIVATE "LINKER:--gc-sections")

pico_add_extra_outputs(bootloader)
pico_set_binary_type(bootloader copy_to_ram)

set_target_properties(bootloader PROPERTIES COMPILE_FLAGS "-Wall")

pico_set_linker_script(bootloader ${CMAKE_CURRENT_SOURCE_DIR}/lib/rp2040-serial-bootloader/bootloader.ld)

target_link_libraries(bootloader
        pico_stdlib
        hardware_dma
        hardware_flash
        hardware_structs
        hardware_resets
        cmsis_core)

set(BOOTLOADER_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/rp2040-serial-bootloader" CACHE INTERNAL "")

function(bootloader_define_library)
    set(NAME bootloader)
    set(ORIGINAL_BIN ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.bin)
    set(BIN_ASM ${CMAKE_CURRENT_BINARY_DIR}/${NAME}_bin.S)

    add_custom_target(${NAME}_bin DEPENDS ${ORIGINAL_BIN})
    add_custom_command(OUTPUT ${ORIGINAL_BIN} DEPENDS ${NAME} COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${NAME}> ${ORIGINAL_BIN})

    find_package (Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(${NAME}_bin_asm DEPENDS ${BIN_ASM})
    add_custom_command(OUTPUT ${BIN_ASM} DEPENDS ${ORIGINAL_BIN}
            COMMAND ${Python3_EXECUTABLE} ${BOOTLOADER_DIR}/mkasm.py ${ORIGINAL_BIN} ${BIN_ASM}
    )

    add_library(${NAME}_library INTERFACE)
    add_dependencies(${NAME}_library ${NAME}_bin_asm)
    # not strictly (or indeed actually) a link library, but this avoids dependency cycle
    target_link_libraries(${NAME}_library INTERFACE ${BIN_ASM})
endfunction()

bootloader_define_library()

function(bootloader_build_combined NAME)
    set(APP ${NAME}_app)
    set(APP_BIN ${CMAKE_CURRENT_BINARY_DIR}/${APP}.bin)
    set(APP_HDR ${CMAKE_CURRENT_BINARY_DIR}/${APP}_hdr.bin)

    set(COMBINED ${NAME}_combined)

    target_link_libraries(${NAME} bootloader_library)

    pico_set_linker_script(${NAME} ${BOOTLOADER_DIR}/combined.ld)

    pico_add_bin_output(${NAME})

    # TODO: The hard-coded 16k here is a bit nasty
    add_custom_target(${APP}_bin DEPENDS ${APP_BIN})
    add_custom_command(OUTPUT ${APP_BIN} DEPENDS ${NAME}.bin
            COMMAND dd ibs=1k iseek=32 if=${NAME}.bin of=${APP_BIN}
    )

    # TODO: The hard-coded address here is a bit nasty
    add_custom_target(${APP}_hdr DEPENDS ${APP}_bin)
    add_custom_command(OUTPUT ${APP_HDR} DEPENDS ${APP_BIN}
            COMMAND ${BOOTLOADER_DIR}/gen_imghdr.py -a 0x10008000 ${APP_BIN} ${APP_HDR}
    )

    add_custom_target(${COMBINED} ALL DEPENDS ${APP_HDR})
    add_custom_command(TARGET ${COMBINED} DEPENDS ${APP_HDR}
            COMMAND ${CMAKE_OBJCOPY} --update-section .app_hdr=${APP_HDR} ${NAME}.elf ${COMBINED}.elf
    )
    add_custom_command(TARGET ${COMBINED} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -Obinary ${COMBINED}.elf ${COMBINED}.bin
    )
    add_custom_command(TARGET ${COMBINED} POST_BUILD
        COMMAND ${PICO_SDK_PATH}/build/_deps/picotool/picotool uf2 convert ${COMBINED}.elf ${COMBINED}.uf2
        VERBATIM)
endfunction()

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/no-OS-FatFS/src build)

add_executable(smart_lcc
        src/smart_lcc.cpp
        src/double_tap.cpp
        src/SystemStatus.cpp
        src/Controller/Core0/SystemSettings.cpp
        src/utils/checksum.cpp
        src/utils/hex_format.cpp
        src/utils/polymath.cpp
        src/utils/triplet.cpp
        src/MulticoreSupport.cpp src/MulticoreSupport.h
        src/Controller/Core0/Protocol/control_board_protocol.cpp
        src/Controller/Core0/Util/HybridController.cpp
        src/Controller/Core0/Util/HysteresisController.cpp
        src/Controller/Core0/Protocol/lcc_protocol.cpp
        src/Controller/Core0/Util/PIDController.cpp
        src/Controller/Core0/SafePacketSender.cpp
        src/Controller/Core0/SystemController.cpp
        src/Controller/Core0/Util/TimedLatch.cpp
        src/utils/UartReadBlockingTimeout.h
        lib/slip/slip.cpp lib/slip/slip.h
        src/Controller/Core1/EspFirmware.cpp src/Controller/Core1/EspFirmware.h
        src/utils/ClearUartCruft.h
        src/utils/crc32.cpp src/utils/crc32.h src/utils/USBDebug.h src/utils/compile_time_crc.hpp
        src/Controller/Core1/MCP9600.cpp
        src/Controller/Core1/MCP9600.h
        src/Controller/Core1/SettingsFlash.cpp
        src/Controller/Core1/SettingsFlash.h
        src/Controller/Core1/SettingsManager.cpp
        src/Controller/Core1/SettingsManager.h
        src/Controller/Core1/Automations.cpp
        src/Controller/Core1/Core1Controller.cpp
        src/Controller/Core1/Core1Controller.h
        src/Routine/RoutineStep.cpp
        src/Routine/RoutineStep.h
        src/Routine/RoutineStepExitCondition.cpp
        src/Routine/RoutineStepExitCondition.h
#        src/Routine/RoutineParser.cpp
#        src/Routine/RoutineParser.h
#        src/Controller/Core1/Core1State.cpp
#        src/Controller/Core1/Core1State.h
#        src/utils/BufferReader.cpp
#        src/utils/BufferReader.h
)

target_include_directories(smart_lcc PUBLIC lib/Ring-Buffer lib/optional-bare src)

target_link_libraries(smart_lcc
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_dma
        pico_multicore
        hardware_clocks
        FatFs_SPI
        )

# enable usb output, disable uart output
# stdio_usb is required to be able to use picotool
pico_enable_stdio_usb(smart_lcc 1)
pico_enable_stdio_uart(smart_lcc 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(smart_lcc)

pico_set_linker_script(smart_lcc ${CMAKE_SOURCE_DIR}/memmap.ld)
#pico_set_linker_script(smart_lcc ${CMAKE_SOURCE_DIR}/memmap-no-boot2.ld)

bootloader_build_combined(smart_lcc)

add_custom_target(Upload-JLink COMMAND /Applications/SEGGER/JLink/JLinkExe -device RP2040_M0_0 -CommandFile ../upload.jlink DEPENDS smart_lcc)
add_custom_target(Upload-Picotool COMMAND picotool load -x smart_lcc.uf2 -f DEPENDS smart_lcc)