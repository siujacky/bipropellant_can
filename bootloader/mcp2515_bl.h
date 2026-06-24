#pragma once

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * MCP2515 SPI command bytes
 * ----------------------------------------------------------------------- */
#define MCP_WRITE       0x02
#define MCP_READ        0x03
#define MCP_BITMOD      0x05
#define MCP_RESET       0xC0
#define MCP_RTS_TXB0    0x81

/* -----------------------------------------------------------------------
 * MCP2515 register addresses
 * ----------------------------------------------------------------------- */
#define MCP_CNF1        0x2A
#define MCP_CNF2        0x29
#define MCP_CNF3        0x28
#define MCP_CANCTRL     0x0F
#define MCP_CANSTAT     0x0E
#define MCP_CANINTE     0x2B
#define MCP_CANINTF     0x2C
#define MCP_RXB0CTRL    0x60
#define MCP_RXB1CTRL    0x70
#define MCP_TXB0CTRL    0x30
#define MCP_EFLG        0x2D

/* -----------------------------------------------------------------------
 * Function prototypes
 * ----------------------------------------------------------------------- */

/**
 * mcp2515_bl_init - Reset and configure the MCP2515 for CAN communication.
 *
 * Must be called once during bootloader startup before any TX/RX operations.
 * Leaves the controller in configuration mode; call mcp2515_bl_enter_normal()
 * when ready to go on-bus.
 *
 * Returns 0 on success, -1 if the device does not respond.
 */
void mcp2515_bl_init(void);

/**
 * mcp2515_bl_enter_normal - Transition the MCP2515 from config to normal mode.
 *
 * Returns 0 on success, -1 on timeout.
 */
int mcp2515_bl_enter_normal(void);

/**
 * mcp2515_bl_tx - Transmit a CAN frame via TXB0.
 *
 * @can_id:  11-bit standard CAN identifier.
 * @data:    Pointer to the payload bytes.
 * @len:     Payload length in bytes (0–8).
 *
 * Returns 0 on success, -1 if the TX buffer is busy or an error occurs.
 */
int mcp2515_bl_tx(uint16_t can_id, const uint8_t *data, uint8_t len);

/**
 * mcp2515_bl_rx - Read one CAN frame from the receive buffer.
 *
 * @can_id:  Output: received 11-bit standard CAN identifier.
 * @data:    Output buffer; must be at least 8 bytes.
 * @len:     Output: number of payload bytes written to @data.
 *
 * Returns 0 on success, -1 if no frame is available or a read error occurs.
 * Check mcp2515_bl_rx_available() first to avoid blocking.
 */
int mcp2515_bl_rx(uint16_t *can_id, uint8_t *data, uint8_t *len);

/**
 * mcp2515_bl_rx_available - Check whether a received frame is waiting.
 *
 * Returns 1 if at least one frame is available in RXB0 or RXB1, 0 otherwise.
 */
int mcp2515_bl_rx_available(void);
