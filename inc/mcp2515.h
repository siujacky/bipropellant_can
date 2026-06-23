#pragma once

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

// MCP2515 Register Addresses
#define MCP2515_RXF0SIDH    0x00
#define MCP2515_RXF0SIDL    0x01
#define MCP2515_RXF1SIDH    0x04
#define MCP2515_RXF1SIDL    0x05
#define MCP2515_RXF2SIDH    0x08
#define MCP2515_RXF2SIDL    0x09
#define MCP2515_BFPCTRL     0x0C
#define MCP2515_TXRTSCTRL   0x0D
#define MCP2515_CANSTAT     0x0E
#define MCP2515_CANCTRL     0x0F
#define MCP2515_CNF3        0x28
#define MCP2515_CNF2        0x29
#define MCP2515_CNF1        0x2A
#define MCP2515_CANINTE     0x2B
#define MCP2515_CANINTF     0x2C
#define MCP2515_EFLG        0x2D
#define MCP2515_TXB0CTRL    0x30
#define MCP2515_TXB1CTRL    0x40
#define MCP2515_TXB2CTRL    0x50
#define MCP2515_RXB0CTRL    0x60
#define MCP2515_RXB1CTRL    0x70

// MCP2515 SPI Commands
#define MCP2515_CMD_RESET       0xC0
#define MCP2515_CMD_READ        0x03
#define MCP2515_CMD_WRITE       0x02
#define MCP2515_CMD_RTS         0x80
#define MCP2515_CMD_READ_STATUS 0xA0
#define MCP2515_CMD_BIT_MODIFY  0x05
#define MCP2515_CMD_RX_STATUS   0xB0

// MCP2515 Operation Modes
#define MCP2515_MODE_NORMAL     0x00
#define MCP2515_MODE_SLEEP      0x20
#define MCP2515_MODE_LOOPBACK   0x40
#define MCP2515_MODE_LISTENONLY 0x60
#define MCP2515_MODE_CONFIG     0x80

// CAN Speed Configuration (8MHz crystal)
#define MCP2515_SPEED_125KBPS   0  // 125 kbps
#define MCP2515_SPEED_250KBPS   1  // 250 kbps
#define MCP2515_SPEED_500KBPS   2  // 500 kbps
#define MCP2515_SPEED_1MBPS     3  // 1 Mbps

// CAN Frame Structure
typedef struct {
    uint32_t id;           // CAN ID (11-bit or 29-bit)
    uint8_t  data[8];      // CAN data (0-8 bytes)
    uint8_t  dlc;          // Data length code (0-8)
    bool     extended;     // Extended frame flag (29-bit ID)
    bool     rtr;          // Remote transmission request
} CAN_Frame;

// MCP2515 Pin Configuration
typedef struct {
    GPIO_TypeDef *MOSI_Port;
    uint16_t      MOSI_Pin;
    GPIO_TypeDef *MISO_Port;
    uint16_t      MISO_Pin;
    GPIO_TypeDef *SCK_Port;
    uint16_t      SCK_Pin;
    GPIO_TypeDef *CS_Port;
    uint16_t      CS_Pin;
    GPIO_TypeDef *INT_Port;
    uint16_t      INT_Pin;
} MCP2515_Config;

// Function Prototypes

// Detection and Initialization
bool MCP2515_Detect(void);
bool MCP2515_Init(uint8_t speed);
void MCP2515_Reset(void);
bool MCP2515_SetMode(uint8_t mode);

// Configuration
bool MCP2515_SetBitrate(uint8_t speed);
// Expected CNF1/2/3 for a given speed (8 MHz xtal) — shared by init + diagnostics.
bool MCP2515_GetExpectedCNF(uint8_t speed, uint8_t *cnf1, uint8_t *cnf2, uint8_t *cnf3);
void MCP2515_SetFilter(uint32_t filter, uint32_t mask);
void MCP2515_EnableInterrupts(uint8_t interrupts);

// Transmit and Receive
bool MCP2515_SendFrame(CAN_Frame *frame);
bool MCP2515_ReceiveFrame(CAN_Frame *frame);
bool MCP2515_Available(void);

// Register Access
uint8_t MCP2515_ReadRegister(uint8_t address);
void MCP2515_WriteRegister(uint8_t address, uint8_t value);
void MCP2515_ModifyRegister(uint8_t address, uint8_t mask, uint8_t value);

// Status
uint8_t MCP2515_GetStatus(void);
uint8_t MCP2515_GetRxStatus(void);
uint8_t MCP2515_GetErrorFlags(void);

// Diagnostic
void MCP2515_LoopbackTest(void);
