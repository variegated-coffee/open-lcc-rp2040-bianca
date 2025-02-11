// Driver for accessing SD card in SDIO mode on RP2040.

#include "ZuluSCSI_platform.h"

#ifdef SD_USE_SDIO

#include <assert.h>
#include <stdint.h>
#include <string.h>
//
#include <hardware/gpio.h>
#include <hardware/clocks.h>
//
#include "sd_card.h"
#include "rp2040_sdio.h"
#include "rp2040_sdio.pio.h"  // build\build\rp2040_sdio.pio.h
#include "SdioCard.h"
#include "util.h"
#include "diskio.h"
#include "my_debug.h"
#include "hardware/gpio.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/structs/pads_bank0.h"

#define STATE sd_card_p->sdio_if_p->state

// #define azlog(...)
// #define azdbg(...)

//FIXME
#define azdbg(arg1, ...) {\
    DBG_PRINTF("%s,%d: %s\n", __func__, __LINE__, arg1); \
}
#define azlog azdbg

#define checkReturnOk(call) ((STATE.error = (call)) == SDIO_OK ? true : logSDError(sd_card_p, __LINE__))

static bool logSDError(sd_card_t *sd_card_p, int line)
{
    STATE.error_line = line;
    azlog("SDIO SD card error on line ", line, ", error code ", (int)STATE.error);
    return false;
}

/*
    CLKDIV is from sd_driver\SDIO\rp2040_sdio.pio

    baud = clk_sys / (CLKDIV * clk_div) 
    baud * CLKDIV * clk_div = clk_sys;
    clk_div = clk_sys / (CLKDIV * baud)
*/
static float calculate_clk_div(uint baud) {
    float div = (float)clock_get_hz(clk_sys) / (CLKDIV * baud);
    /* Baud rate cannot exceed clk_sys frequency divided by CLKDIV! */
    assert(div >= 1 && div <= 65536);
    return div;
}

bool sd_sdio_begin(sd_card_t *sd_card_p)
{
    uint32_t reply;
    sdio_status_t status;
    
    // Initialize at 400 kHz clock speed
    if (!rp2040_sdio_init(sd_card_p, calculate_clk_div(400 * 1000)))
        return false; 

    // Establish initial connection with the card
    for (int retries = 0; retries < 5; retries++)
    {
        delayMicroseconds(1000);
        reply = 0;
        rp2040_sdio_command_R1(sd_card_p, CMD0, 0, NULL); // GO_IDLE_STATE
        status = rp2040_sdio_command_R1(sd_card_p, CMD8, 0x1AA, &reply); // SEND_IF_COND

        if (status == SDIO_OK && reply == 0x1AA)
        {
            break;
        }
    }

    if (reply != 0x1AA || status != SDIO_OK)
    {
        // azdbg("SDIO not responding to CMD8 SEND_IF_COND, status ", (int)status, " reply ", reply);
        EMSG_PRINTF("%s,%d SDIO not responding to CMD8 SEND_IF_COND, status 0x%x reply 0x%lx\n", 
            __func__, __LINE__, status, reply);
        return false;
    }

    // Send ACMD41 to begin card initialization and wait for it to complete
    // uint32_t start = millis();
    absolute_time_t timeout_time = make_timeout_time_ms(1000);
    do {
        if (!checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD55, 0, &reply)) || // APP_CMD
            !checkReturnOk(rp2040_sdio_command_R3(sd_card_p, ACMD41, 0xD0040000, &STATE.ocr))) // 3.0V voltage
            // !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, ACMD41, 0xC0100000, &STATE.ocr)))
        {
            return false;
        }

        // if ((uint32_t)(millis() - start) > 1000)
        if (absolute_time_diff_us(get_absolute_time(), timeout_time) <= 0)
        {
            azlog("SDIO card initialization timeout");
            return false;
        }
    } while (!(STATE.ocr & (1 << 31)));

    // Get CID
    if (!checkReturnOk(rp2040_sdio_command_R2(sd_card_p, CMD2, 0, (uint8_t *)&sd_card_p->cid)))
    {
        azdbg("SDIO failed to read CID");
        return false;
    }

    // Get relative card address
    if (!checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD3, 0, &STATE.rca)))
    {
        azdbg("SDIO failed to get RCA");
        return false;
    }

    // Get CSD
    if (!checkReturnOk(rp2040_sdio_command_R2(sd_card_p, CMD9, STATE.rca, sd_card_p->csd.csd)))
    {
        azdbg("SDIO failed to read CSD");
        return false;
    }

    sd_card_p->sectors = sd_sdio_sectorCount(sd_card_p);

    // Select card
    if (!checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD7, STATE.rca, &reply)))
    {
        azdbg("SDIO failed to select card");
        return false;
    }

    // Set 4-bit bus mode
    if (!checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD55, STATE.rca, &reply)) ||
        !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, ACMD6, 2, &reply)))
    {
        azdbg("SDIO failed to set bus width");
        return false;
    }
    if (!checkReturnOk(rp2040_sdio_command_R1(sd_card_p, 16, 512, &reply))) // SET_BLOCKLEN
    {
        EMSG_PRINTF("%s,%d SDIO failed to set BLOCKLEN\n", __func__, __LINE__);
        return false;
    }
    // Increase to high clock rate
    if (!sd_card_p->sdio_if_p->baud_rate)
        sd_card_p->sdio_if_p->baud_rate = 10*1000*1000; // 10 MHz default
    if (!rp2040_sdio_init(sd_card_p, calculate_clk_div(sd_card_p->sdio_if_p->baud_rate)))
        return false; 

    return true;
}

uint8_t sd_sdio_errorCode(sd_card_t *sd_card_p) // const
{
    return STATE.error;
}

uint32_t sd_sdio_errorData() // const
{
    return 0;
}

uint32_t sd_sdio_errorLine(sd_card_t *sd_card_p) // const
{
    return STATE.error_line;
}

bool sd_sdio_isBusy(sd_card_t *sd_card_p) 
{
    // return (sio_hw->gpio_in & (1 << SDIO_D0)) == 0;
    return (sio_hw->gpio_in & (1 << sd_card_p->sdio_if_p->D0_gpio)) == 0;
}

uint32_t sd_sdio_kHzSdClk(sd_card_t *sd_card_p)
{
    return 0;
}

bool sd_sdio_readOCR(sd_card_t *sd_card_p, uint32_t* ocr)
{
    // SDIO mode does not have CMD58, but main program uses this to
    // poll for card presence. Return status register instead.
    return checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD13, STATE.rca, ocr));
}

bool sd_sdio_readData(sd_card_t *sd_card_p, uint8_t* dst)
{
    azlog("sd_sdio_readData() called but not implemented!");
    return false;
}

// bool sd_sdio_readStart(sd_card_t *sd_card_p, uint32_t sector)
// {
//     azlog("sd_sdio_readStart() called but not implemented!");
//     return false;
// }

// bool sd_sdio_readStop(sd_card_t *sd_card_p)
// {
//     azlog("sd_sdio_readStop() called but not implemented!");
//     return false;
// }

uint64_t sd_sdio_sectorCount(sd_card_t *sd_card_p)
{
    // return g_sdio_csd.capacity();
    return CSD_capacity(&sd_card_p->csd);
}

uint32_t sd_sdio_status(sd_card_t *sd_card_p)
{
    uint32_t reply;
    if (checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD13, STATE.rca, &reply)))
        return reply;
    else
        return 0;
}

bool sd_sdio_stopTransmission(sd_card_t *sd_card_p, bool blocking)
{
    uint32_t reply;
    if (!checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD12, 0, &reply)))
    {
        return false;
    }

    if (!blocking)
    {
        return true;
    }
    else
    {
        // uint32_t end = millis() + 100;
        absolute_time_t timeout_time = make_timeout_time_ms(200); // CK3: doubled
        // while (millis() < end && sd_sdio_isBusy(sd_card_p))
        while (0 < absolute_time_diff_us(get_absolute_time(), timeout_time) && sd_sdio_isBusy(sd_card_p))
        {
        }
        if (sd_sdio_isBusy(sd_card_p))
        {
            azlog("sd_sdio_stopTransmission() timeout");
            return false;
        }
        else
        {
            return true;
        }
    }
}

bool sd_sdio_syncDevice(sd_card_t *sd_card_p)
{
    return true;
}

uint8_t sd_sdio_type(sd_card_t *sd_card_p) // const
{
    if (STATE.ocr & (1 << 30))
        return SD_CARD_TYPE_SDHC;
    else
        return SD_CARD_TYPE_SD2;
}

bool sd_sdio_writeData(sd_card_t *sd_card_p, const uint8_t* src)
{
    azlog("sd_sdio_writeData() called but not implemented!");
    return false;
}

// bool sd_sdio_writeStart(sd_card_t *sd_card_p, uint32_t sector)
// {
//     azlog("sd_sdio_writeStart() called but not implemented!");
//     return false;
// }

// bool sd_sdio_writeStop(sd_card_t *sd_card_p)
// {
//     azlog("sd_sdio_writeStop() called but not implemented!");
//     return false;
// }

bool sd_sdio_erase(sd_card_t *sd_card_p, uint32_t firstSector, uint32_t lastSector)
{
    azlog("sd_sdio_erase() not implemented");
    return false;
}

bool sd_sdio_cardCMD6(sd_card_t *sd_card_p, uint32_t arg, uint8_t* status) {
    azlog("sd_sdio_cardCMD6() not implemented");
    return false;
}

bool sd_sdio_readSCR(sd_card_t *sd_card_p, scr_t* scr) {
    azlog("sd_sdio_readSCR() not implemented");
    return false;
}

/* Writing and reading */

bool sd_sdio_writeSector(sd_card_t *sd_card_p, uint32_t sector, const uint8_t* src)
{
    if (((uint32_t)src & 3) != 0)
    {
        // Buffer is not aligned, need to memcpy() the data to a temporary buffer.
        memcpy(STATE.dma_buf, src, sizeof(STATE.dma_buf));
        src = (uint8_t*)STATE.dma_buf;
    }

    uint32_t reply;
    if (/* !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, 16, 512, &reply)) || // SET_BLOCKLEN */
        !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD24, sector, &reply)) || // WRITE_BLOCK
        !checkReturnOk(rp2040_sdio_tx_start(sd_card_p, src, 1))) // Start transmission
    {
        return false;
    }

    do {
        uint32_t bytes_done;
        STATE.error = rp2040_sdio_tx_poll(sd_card_p, &bytes_done);
    } while (STATE.error == SDIO_BUSY);

    if (STATE.error != SDIO_OK)
    {
        azlog("sd_sdio_writeSector(", sector, ") failed: ", (int)STATE.error);
    }

    return STATE.error == SDIO_OK;
}

bool sd_sdio_writeSectors(sd_card_t *sd_card_p, uint32_t sector, const uint8_t* src, size_t n)
{
    if (((uint32_t)src & 3) != 0)
    {
        // Unaligned write, execute sector-by-sector
        for (size_t i = 0; i < n; i++)
        {
            if (!sd_sdio_writeSector(sd_card_p, sector + i, src + 512 * i))
            {
                return false;
            }
        }
        return true;
    }

    uint32_t reply;
    if (/* !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, 16, 512, &reply)) || // SET_BLOCKLEN */
        !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD55, STATE.rca, &reply)) || // APP_CMD
        !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, ACMD23, n, &reply)) || // SET_WR_CLK_ERASE_COUNT
        !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD25, sector, &reply)) || // WRITE_MULTIPLE_BLOCK
        !checkReturnOk(rp2040_sdio_tx_start(sd_card_p, src, n))) // Start transmission
    {
        return false;
    }

    do {
        uint32_t bytes_done;
        STATE.error = rp2040_sdio_tx_poll(sd_card_p, &bytes_done);
    } while (STATE.error == SDIO_BUSY);

    if (STATE.error != SDIO_OK)
    {
        azlog("sd_sdio_writeSectors(", sector, ",...,", (int)n, ") failed: ", (int)STATE.error);
        sd_sdio_stopTransmission(sd_card_p, true);
        return false;
    }
    else
    {
        return sd_sdio_stopTransmission(sd_card_p, true);
    }
}

bool sd_sdio_readSector(sd_card_t *sd_card_p, uint32_t sector, uint8_t* dst)
{
    uint8_t *real_dst = dst;
    if (((uint32_t)dst & 3) != 0)
    {
        // Buffer is not aligned, need to memcpy() the data from a temporary buffer.
        dst = (uint8_t*)STATE.dma_buf;
    }
    uint32_t reply;
    if (/* !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, 16, 512, &reply)) || // SET_BLOCKLEN */
        !checkReturnOk(rp2040_sdio_rx_start(sd_card_p, dst, 1)) || // Prepare for reception
        !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD17, sector, &reply))) // READ_SINGLE_BLOCK
    {
        return false;
    }

    do {
        uint32_t bytes_done;
        STATE.error = rp2040_sdio_rx_poll(sd_card_p, &bytes_done);
    } while (STATE.error == SDIO_BUSY);

    if (STATE.error != SDIO_OK)
    {
        // azlog("sd_sdio_readSector(", sector, ") failed: ", (int)STATE.error);
        EMSG_PRINTF("%s,%d sd_sdio_readSector(%lu) failed: %d\n", 
            __func__, __LINE__, sector, STATE.error);
    }

    if (dst != real_dst)
    {
        memcpy(real_dst, STATE.dma_buf, sizeof(STATE.dma_buf));
    }

    return STATE.error == SDIO_OK;
}

bool sd_sdio_readSectors(sd_card_t *sd_card_p, uint32_t sector, uint8_t* dst, size_t n)
{
    if (((uint32_t)dst & 3) != 0 || sector + n >= sd_card_p->sectors)
    {
        // Unaligned read or end-of-drive read, execute sector-by-sector
        for (size_t i = 0; i < n; i++)
        {
            if (!sd_sdio_readSector(sd_card_p, sector + i, dst + 512 * i))
            {
                return false;
            }
        }
        return true;
    }

    uint32_t reply;
    if (/* !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, 16, 512, &reply)) || // SET_BLOCKLEN */
        !checkReturnOk(rp2040_sdio_rx_start(sd_card_p, dst, n)) || // Prepare for reception
        !checkReturnOk(rp2040_sdio_command_R1(sd_card_p, CMD18, sector, &reply))) // READ_MULTIPLE_BLOCK
    {
        return false;
    }

    do {
        uint32_t bytes_done;
        STATE.error = rp2040_sdio_rx_poll(sd_card_p, &bytes_done);
    } while (STATE.error == SDIO_BUSY);

    if (STATE.error != SDIO_OK)
    {
        // azlog("sd_sdio_readSectors(", sector, ",...,", (int)n, ") failed: ", (int)STATE.error);
        EMSG_PRINTF("sd_sdio_readSectors(%ld,...,%d)  failed: %d\n", sector, n, STATE.error);
        sd_sdio_stopTransmission(sd_card_p, true);
        return false;
    }
    else
    {
        return sd_sdio_stopTransmission(sd_card_p, true);
    }
}

static bool sd_sdio_test_com(sd_card_t *sd_card_p) {
    bool success = false;

    if (!(sd_card_p->m_Status & STA_NOINIT)) {
        // SD card is currently initialized

        // Get status
        uint32_t reply = 0;
        sdio_status_t status = rp2040_sdio_command_R1(sd_card_p, CMD13, STATE.rca, &reply);

        // Only care that communication succeeded
        success = (status == SDIO_OK);

        if (!success) {
            // Card no longer sensed - ensure card is initialized once re-attached
            sd_card_p->m_Status |= STA_NOINIT;
        }
    } else {
        // Do a "light" version of init, just enough to test com

        // Initialize at 400 kHz clock speed
        if (!rp2040_sdio_init(sd_card_p, calculate_clk_div(400 * 1000)))
            return false; 

        // Establish initial connection with the card
        rp2040_sdio_command_R1(sd_card_p, CMD0, 0, NULL); // GO_IDLE_STATE
        uint32_t reply = 0;
        sdio_status_t status = rp2040_sdio_command_R1(sd_card_p, CMD8, 0x1AA, &reply); // SEND_IF_COND

        success = (reply == 0x1AA && status == SDIO_OK);
    }

    return success;
}

static int sd_sdio_init(sd_card_t *sd_card_p) {
    // bool sd_sdio_begin(sd_card_t *sd_card_p);
    bool rc = sd_sdio_begin(sd_card_p);
    if (rc) {
        // The card is now initialized
        sd_card_p->m_Status &= ~STA_NOINIT;
    }
    return sd_card_p->m_Status;
}
static int sd_sdio_write_blocks(sd_card_t *sd_card_p, const uint8_t *buffer,
                                uint64_t ulSectorNumber, uint32_t blockCnt) {
    // bool sd_sdio_writeSectors(sd_card_t *sd_card_p, uint32_t sector, const uint8_t* src, size_t ns);
    bool rc;
    if (1 == blockCnt)
        rc = sd_sdio_writeSector(sd_card_p, ulSectorNumber, buffer);
    else
        rc = sd_sdio_writeSectors(sd_card_p, ulSectorNumber, buffer, blockCnt);
    if (rc)
        return SD_BLOCK_DEVICE_ERROR_NONE;
    else
        return SD_BLOCK_DEVICE_ERROR_WRITE;                                    
}
static int sd_sdio_read_blocks(sd_card_t *sd_card_p, uint8_t *buffer, uint64_t ulSectorNumber,
                               uint32_t ulSectorCount) {
    // bool sd_sdio_readSectors(sd_card_t *sd_card_p, uint32_t sector, uint8_t* dst, size_t n)
    bool rc;
    if (1 == ulSectorCount) 
        rc = sd_sdio_readSector(sd_card_p, ulSectorNumber, buffer);
    else        
        rc= sd_sdio_readSectors(sd_card_p, ulSectorNumber, buffer, ulSectorCount);
    if (rc)
        return SD_BLOCK_DEVICE_ERROR_NONE;
    else
        return SD_BLOCK_DEVICE_ERROR_NO_RESPONSE;
}

// Helper function to configure whole GPIO in one line
static void gpio_conf(uint gpio, gpio_function_t fn, bool pullup, bool pulldown, bool output, bool initial_state, bool fast_slew)
{
    gpio_set_function(gpio, fn);
    gpio_set_pulls(gpio, pullup, pulldown);
    
    if (output) {
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, initial_state);
    }
    
    if (fast_slew) {
        pads_bank0_hw->io[gpio] |= PADS_BANK0_GPIO0_SLEWFAST_BITS;
    }
}
void sd_sdio_ctor(sd_card_t *sd_card_p) {
    assert(sd_card_p->sdio_if_p); // Must have an interface object
    /*
    Pins CLK_gpio, D1_gpio, D2_gpio, and D3_gpio are at offsets from pin D0_gpio.
    The offsets are determined by sd_driver\SDIO\rp2040_sdio.pio.
    */
    assert(!sd_card_p->sdio_if_p->CLK_gpio);
    assert(!sd_card_p->sdio_if_p->D1_gpio);
    assert(!sd_card_p->sdio_if_p->D2_gpio);
    assert(!sd_card_p->sdio_if_p->D3_gpio);

    sd_card_p->sdio_if_p->CLK_gpio = (sd_card_p->sdio_if_p->D0_gpio + SDIO_CLK_PIN_D0_OFFSET) % 32;
    sd_card_p->sdio_if_p->D1_gpio = sd_card_p->sdio_if_p->D0_gpio + 1;
    sd_card_p->sdio_if_p->D2_gpio = sd_card_p->sdio_if_p->D0_gpio + 2;
    sd_card_p->sdio_if_p->D3_gpio = sd_card_p->sdio_if_p->D0_gpio + 3;

    sd_card_p->m_Status = STA_NOINIT;

    sd_card_p->init = sd_sdio_init;
    sd_card_p->write_blocks = sd_sdio_write_blocks;
    sd_card_p->read_blocks = sd_sdio_read_blocks;
    sd_card_p->get_num_sectors = sd_sdio_sectorCount;
    sd_card_p->sd_test_com = sd_sdio_test_com;

    //        pin                          function        pup   pdown  out    state fast
    gpio_conf(sd_card_p->sdio_if_p->CLK_gpio, GPIO_FUNC_PIO1, true, false, true,  true, true);
    gpio_conf(sd_card_p->sdio_if_p->CMD_gpio, GPIO_FUNC_PIO1, true, false, true,  true, true);
    gpio_conf(sd_card_p->sdio_if_p->D0_gpio,  GPIO_FUNC_PIO1, true, false, false, true, true);
    gpio_conf(sd_card_p->sdio_if_p->D1_gpio,  GPIO_FUNC_PIO1, true, false, false, true, true);
    gpio_conf(sd_card_p->sdio_if_p->D2_gpio,  GPIO_FUNC_PIO1, true, false, false, true, true);
    gpio_conf(sd_card_p->sdio_if_p->D3_gpio,  GPIO_FUNC_PIO1, true, false, false, true, true);
}

#endif