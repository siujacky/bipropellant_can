#include "mcp2515.h"
#include "software_spi.h"
#include <string.h>
#include <stdio.h>

// Private function prototypes
static void MCP2515_Select(void);
static void MCP2515_Deselect(void);
static uint8_t MCP2515_SPITransfer(uint8_t data);

// MCP2515 Initialization and Detection
bool MCP2515_Detect(void) {
    // Initialize software SPI
    SoftSPI_Init();
    SoftSPI_SetMode(SPI_MODE0);
    
    // Small delay for MCP2515 to power up
    HAL_Delay(10);
    
    // Try to read CANSTAT register (should be 0x80 after reset - CONFIG mode)
    uint8_t canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
    
    if ((canstat & 0xE0) == 0x80) {
        // CANSTAT indicates CONFIG mode - MCP2515 detected
        return true;
    }
    
    // Try resetting the chip
    MCP2515_Reset();
    HAL_Delay(10);
    
    // Check again
    canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
    if ((canstat & 0xE0) == 0x80) {
        return true;
    }
    
    // Try read/write test to verify communication
    MCP2515_WriteRegister(MCP2515_CNF1, 0xAA);
    uint8_t test = MCP2515_ReadRegister(MCP2515_CNF1);
    
    if (test == 0xAA) {
        MCP2515_WriteRegister(MCP2515_CNF1, 0x55);
        test = MCP2515_ReadRegister(MCP2515_CNF1);
        if (test == 0x55) {
            return true;
        }
    }
    
    // No MCP2515 detected - deinitialize SPI
    SoftSPI_Deinit();
    return false;
}

bool MCP2515_Init(uint8_t speed) {
    // Reset MCP2515
    MCP2515_Reset();
    HAL_Delay(10);

    // Set configuration mode
    if (!MCP2515_SetMode(MCP2515_MODE_CONFIG)) {
        return false;
    }

    // Configure bit timing
    if (!MCP2515_SetBitrate(speed)) {
        return false;
    }

    // Configure RX buffers - receive all messages. 0x64 sets BUKT (rollover):
    // a 2nd frame spills into RXB1 instead of being dropped while RXB0 is drained.
    MCP2515_WriteRegister(MCP2515_RXB0CTRL, 0x64);  // RXM=11 (accept all) + BUKT
    MCP2515_WriteRegister(MCP2515_RXB1CTRL, 0x60);  // RXM=11
    MCP2515_WriteRegister(MCP2515_CANINTF, 0x00);
    MCP2515_WriteRegister(MCP2515_CANINTE, 0x03);

    // Set normal mode
    if (!MCP2515_SetMode(MCP2515_MODE_NORMAL)) {
        return false;
    }

    return true;
}

void MCP2515_Reset(void) {
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_RESET);
    MCP2515_Deselect();
    HAL_Delay(10);
}

bool MCP2515_SetMode(uint8_t mode) {
    MCP2515_ModifyRegister(MCP2515_CANCTRL, 0xE0, mode);
    
    // Verify mode change
    uint32_t timeout = HAL_GetTick() + 100;
    while (HAL_GetTick() < timeout) {
        uint8_t canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
        if ((canstat & 0xE0) == mode) {
            return true;
        }
        HAL_Delay(1);
    }
    
    return false;
}

// Single source of truth for the CNF (bit-timing) registers, indexed by speed.
// Values are for an 8 MHz crystal. Used both to program the controller and to
// verify the readback in diagnostics, so the two can never drift apart.
bool MCP2515_GetExpectedCNF(uint8_t speed, uint8_t *cnf1, uint8_t *cnf2, uint8_t *cnf3) {
    // CNF1: SJW=1, BRP / CNF2: BTLMODE=1, SAM=0, PHSEG1, PRSEG / CNF3: PHSEG2
    switch (speed) {
        case MCP2515_SPEED_125KBPS: *cnf1 = 0x01; *cnf2 = 0xB1; *cnf3 = 0x85; break;
        case MCP2515_SPEED_250KBPS: *cnf1 = 0x00; *cnf2 = 0xB1; *cnf3 = 0x85; break;
        case MCP2515_SPEED_500KBPS: *cnf1 = 0x00; *cnf2 = 0x90; *cnf3 = 0x82; break;
        case MCP2515_SPEED_1MBPS:   *cnf1 = 0x00; *cnf2 = 0x80; *cnf3 = 0x80; break;
        default: return false;
    }
    return true;
}

bool MCP2515_SetBitrate(uint8_t speed) {
    uint8_t cnf1, cnf2, cnf3;
    if (!MCP2515_GetExpectedCNF(speed, &cnf1, &cnf2, &cnf3)) {
        return false;
    }

    MCP2515_WriteRegister(MCP2515_CNF1, cnf1);
    MCP2515_WriteRegister(MCP2515_CNF2, cnf2);
    MCP2515_WriteRegister(MCP2515_CNF3, cnf3);

    return true;
}

bool MCP2515_SendFrame(CAN_Frame *frame) {
    // Pick a FREE TX buffer among TXB0/1/2 instead of only TXB0. Sending the 3
    // status frames (speed/pos/batt) back-to-back to a single buffer dropped the
    // 2nd and 3rd every cycle while TXB0 was still transmitting. CTRL regs 0x30/
    // 0x40/0x50, SIDH load addr = CTRL+1, RTS = 0x80|(1<<n).
    static const uint8_t TXB_CTRL[3] = {0x30, 0x40, 0x50};
    int buf = -1;
    for (int b = 0; b < 3; b++) {
        if (!(MCP2515_ReadRegister(TXB_CTRL[b]) & 0x08)) { buf = b; break; }  // TXREQ clear
    }
    if (buf < 0) return false;             // all 3 buffers busy (bus stuck / no ACK)
    uint8_t sidh_addr = TXB_CTRL[buf] + 1; // TXBnSIDH
    uint8_t rts_cmd   = MCP2515_CMD_RTS | (uint8_t)(1u << buf);

    // Load TX buffer
    uint8_t txbuf[13];
    uint8_t idx = 0;
    
    if (frame->extended) {
        // Extended frame (29-bit ID)
        txbuf[idx++] = (uint8_t)(frame->id >> 21);           // SIDH
        txbuf[idx++] = (uint8_t)(((frame->id >> 13) & 0xE0) | 
                                  0x08 |                       // EXIDE bit
                                  ((frame->id >> 16) & 0x03)); // EID17:16
        txbuf[idx++] = (uint8_t)(frame->id >> 8);             // EID15:8
        txbuf[idx++] = (uint8_t)(frame->id);                  // EID7:0
    } else {
        // Standard frame (11-bit ID)
        txbuf[idx++] = (uint8_t)(frame->id >> 3);             // SIDH
        txbuf[idx++] = (uint8_t)(frame->id << 5);             // SIDL
        txbuf[idx++] = 0x00;                                   // EID8
        txbuf[idx++] = 0x00;                                   // EID0
    }
    
    // DLC
    txbuf[idx++] = frame->rtr ? (0x40 | frame->dlc) : frame->dlc;
    
    // Data bytes
    for (uint8_t i = 0; i < frame->dlc && i < 8; i++) {
        txbuf[idx++] = frame->data[i];
    }
    
    // Write to the chosen TX buffer
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_WRITE);
    MCP2515_SPITransfer(sidh_addr);   // TXBnSIDH
    for (uint8_t i = 0; i < idx; i++) {
        MCP2515_SPITransfer(txbuf[i]);
    }
    MCP2515_Deselect();

    // Request to send (RTS for the chosen buffer)
    MCP2515_Select();
    MCP2515_SPITransfer(rts_cmd);
    MCP2515_Deselect();

    return true;
}

bool MCP2515_ReceiveFrame(CAN_Frame *frame) {
    // Check RX status
    uint8_t status = MCP2515_GetRxStatus();

    if (!(status & 0xC0)) {
        // No message available
        return false;
    }

    uint8_t rxbuf[13];

    // Use "Read RX Buffer" instruction (0x90/0x94)
    uint8_t read_cmd = (status & 0x40) ? 0x90 : 0x94;

    MCP2515_Select();
    MCP2515_SPITransfer(read_cmd);
    for (uint8_t i = 0; i < 13; i++) {
        rxbuf[i] = MCP2515_SPITransfer(0xFF);
    }
    MCP2515_Deselect();


    // Parse frame
    if (rxbuf[1] & 0x08) {
        // Extended frame
        frame->extended = true;
        frame->id = ((uint32_t)rxbuf[0] << 21) |
                    ((uint32_t)(rxbuf[1] & 0xE0) << 13) |
                    ((uint32_t)(rxbuf[1] & 0x03) << 16) |
                    ((uint32_t)rxbuf[2] << 8) |
                    rxbuf[3];
    } else {
        // Standard frame
        frame->extended = false;
        frame->id = ((uint32_t)rxbuf[0] << 3) |
                    (rxbuf[1] >> 5);
    }
    
    frame->rtr = (rxbuf[4] & 0x40) ? true : false;
    frame->dlc = rxbuf[4] & 0x0F;
    
    // Copy data
    for (uint8_t i = 0; i < frame->dlc && i < 8; i++) {
        frame->data[i] = rxbuf[5 + i];
    }
    
    // Clear interrupt flag
    if (status & 0x40) {
        MCP2515_ModifyRegister(MCP2515_CANINTF, 0x01, 0x00);  // Clear RXB0 flag
    } else {
        MCP2515_ModifyRegister(MCP2515_CANINTF, 0x02, 0x00);  // Clear RXB1 flag
    }
    
    return true;
}

bool MCP2515_Available(void) {
    uint8_t status = MCP2515_GetRxStatus();
    return (status & 0xC0) != 0;
}

uint8_t MCP2515_ReadRegister(uint8_t address) {
    uint8_t result;
    
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_READ);
    MCP2515_SPITransfer(address);
    result = MCP2515_SPITransfer(0xFF);
    MCP2515_Deselect();
    
    return result;
}

void MCP2515_WriteRegister(uint8_t address, uint8_t value) {
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_WRITE);
    MCP2515_SPITransfer(address);
    MCP2515_SPITransfer(value);
    MCP2515_Deselect();
}

void MCP2515_ModifyRegister(uint8_t address, uint8_t mask, uint8_t value) {
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_BIT_MODIFY);
    MCP2515_SPITransfer(address);
    MCP2515_SPITransfer(mask);
    MCP2515_SPITransfer(value);
    MCP2515_Deselect();
}

uint8_t MCP2515_GetStatus(void) {
    uint8_t status;
    
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_READ_STATUS);
    status = MCP2515_SPITransfer(0xFF);
    MCP2515_Deselect();
    
    return status;
}

uint8_t MCP2515_GetRxStatus(void) {
    uint8_t status;

    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_RX_STATUS);
    status = MCP2515_SPITransfer(0xFF);
    MCP2515_Deselect();

    return status;
}

uint8_t MCP2515_GetErrorFlags(void) {
    return MCP2515_ReadRegister(MCP2515_EFLG);
}

// Loopback test - call AFTER software serial is initialized
void MCP2515_LoopbackTest(void) {
    extern void forceLog(char *message);
    char msg[100];

    forceLog("\r\n=== LOOPBACK TEST ===\r\n");

    // Save current mode
    uint8_t saved_canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
    sprintf(msg, "Current CANSTAT: 0x%02X\r\n", saved_canstat);
    forceLog(msg);

    // Enter config mode first
    if (!MCP2515_SetMode(MCP2515_MODE_CONFIG)) {
        forceLog("Failed to enter config mode!\r\n");
        return;
    }

    // Clear any pending RX
    MCP2515_WriteRegister(MCP2515_CANINTF, 0x00);

    // Enter loopback mode
    if (!MCP2515_SetMode(MCP2515_MODE_LOOPBACK)) {
        forceLog("Failed to enter loopback mode!\r\n");
        MCP2515_SetMode(MCP2515_MODE_NORMAL);
        return;
    }
    forceLog("Loopback mode set, sending test frame...\r\n");

    // Send a test frame (will loop back to RX)
    CAN_Frame test_frame;
    test_frame.id = 0x123;
    test_frame.extended = false;
    test_frame.rtr = false;
    test_frame.dlc = 4;
    test_frame.data[0] = 0xDE;
    test_frame.data[1] = 0xAD;
    test_frame.data[2] = 0xBE;
    test_frame.data[3] = 0xEF;

    if (MCP2515_SendFrame(&test_frame)) {
        forceLog("Test frame sent, waiting for loopback...\r\n");
        HAL_Delay(50);  // Wait for loopback

        // Check if received
        uint8_t rxstat = MCP2515_GetRxStatus();
        sprintf(msg, "RX status after loopback: 0x%02X\r\n", rxstat);
        forceLog(msg);

        if (rxstat & 0xC0) {
            // Read the RX buffer manually
            uint8_t rxbuf[13];
            MCP2515_Select();
            MCP2515_SPITransfer(MCP2515_CMD_READ);
            MCP2515_SPITransfer(0x61);  // RXB0SIDH
            for (int i = 0; i < 13; i++) {
                rxbuf[i] = MCP2515_SPITransfer(0xFF);
            }
            MCP2515_Deselect();

            sprintf(msg, "Loopback RX: %02X %02X %02X %02X %02X | %02X %02X %02X %02X\r\n",
                    rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], rxbuf[4],
                    rxbuf[5], rxbuf[6], rxbuf[7], rxbuf[8]);
            forceLog(msg);

            // Expected for ID=0x123: SIDH=0x24, SIDL=0x60, data at [5-8]
            if (rxbuf[0] == 0x24 && rxbuf[1] == 0x60 &&
                rxbuf[5] == 0xDE && rxbuf[6] == 0xAD) {
                forceLog("*** LOOPBACK SUCCESS! ***\r\n");
                forceLog("SPI RX buffer reads work correctly.\r\n");
                forceLog("Problem must be CAN bus reception.\r\n");
            } else {
                forceLog("*** LOOPBACK FAILED - DATA MISMATCH! ***\r\n");
                sprintf(msg, "Expected: 24 60 xx xx 04 DE AD BE EF\r\n");
                forceLog(msg);
                forceLog("SPI RX buffer reads are corrupted.\r\n");
            }

            // Clear RX flag
            MCP2515_ModifyRegister(MCP2515_CANINTF, 0x01, 0x00);
        } else {
            forceLog("No loopback message received!\r\n");
        }
    } else {
        forceLog("Failed to send test frame!\r\n");
    }

    forceLog("=== END LOOPBACK TEST ===\r\n");

    // Return to normal mode
    MCP2515_SetMode(MCP2515_MODE_NORMAL);
    forceLog("Returned to normal mode.\r\n\r\n");
}

// Private functions
static void MCP2515_Select(void) {
    SoftSPI_CS_Low();
}

static void MCP2515_Deselect(void) {
    SoftSPI_CS_High();
}

static uint8_t MCP2515_SPITransfer(uint8_t data) {
    return SoftSPI_Transfer(data);
}
