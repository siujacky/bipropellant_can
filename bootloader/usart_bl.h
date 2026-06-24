#pragma once
#include <stdint.h>

/* -------------------------------------------------------------------------
 * usart_bl.h — bare-metal USART driver for the STM32F103 bootloader.
 *
 * Polling only (no interrupts, no DMA, no HAL).
 * Mirrors the interface style of mcp2515_bl.h so the bootloader main
 * loop can swap transport backends without changing calling conventions.
 *
 * Default pin assignment (USART1, remapped = 0):
 *   TX = PA9   GPIOA CRH bits[7:4]   output 50MHz AF-PP = 0xB
 *   RX = PA10  GPIOA CRH bits[11:8]  input floating     = 0x4
 *
 * Baud rate is computed from the APB2 clock supplied to usart_bl_init().
 * -------------------------------------------------------------------------
 */

/* -------------------------------------------------------------------------
 * usart_bl_init - Configure USART1 GPIO, clock, and baud rate.
 *
 * @apb2_hz:  APB2 peripheral clock in Hz (e.g. 72000000 for 72 MHz).
 * @baud:     Desired baud rate (e.g. 115200).
 *
 * Must be called once before any TX/RX operations.
 * Leaves USART1 enabled with TX and RX active.
 * -------------------------------------------------------------------------
 */
void usart_bl_init(uint32_t apb2_hz, uint32_t baud);

/* -------------------------------------------------------------------------
 * usart_bl_tx_byte - Transmit one byte, blocking until the TX register
 * is empty.
 *
 * @byte:  Byte to send.
 * -------------------------------------------------------------------------
 */
void usart_bl_tx_byte(uint8_t byte);

/* -------------------------------------------------------------------------
 * usart_bl_tx - Transmit a buffer of bytes.
 *
 * @data:  Pointer to the data to send.
 * @len:   Number of bytes to send.
 *
 * Blocks until all bytes have been loaded into the TX register.
 * -------------------------------------------------------------------------
 */
void usart_bl_tx(const uint8_t *data, uint32_t len);

/* -------------------------------------------------------------------------
 * usart_bl_rx_byte - Receive one byte, blocking until data arrives or the
 * timeout elapses.
 *
 * @out:        Output: byte received.
 * @timeout_ms: Maximum time to wait in milliseconds.
 *
 * Returns 0 on success, -1 on timeout.
 * -------------------------------------------------------------------------
 */
int usart_bl_rx_byte(uint8_t *out, uint32_t timeout_ms);

/* -------------------------------------------------------------------------
 * usart_bl_rx - Receive exactly len bytes into buf.
 *
 * @buf:        Output buffer; must be at least len bytes.
 * @len:        Number of bytes to receive.
 * @timeout_ms: Per-byte timeout in milliseconds.
 *
 * Returns 0 if all bytes were received before any per-byte timeout,
 * -1 on the first byte that timed out.
 * -------------------------------------------------------------------------
 */
int usart_bl_rx(uint8_t *buf, uint32_t len, uint32_t timeout_ms);

/* -------------------------------------------------------------------------
 * usart_bl_rx_available - Check whether a received byte is waiting in the
 * USART data register.
 *
 * Returns 1 if RXNE (read data register not empty) is set, 0 otherwise.
 * -------------------------------------------------------------------------
 */
int usart_bl_rx_available(void);

/* -------------------------------------------------------------------------
 * usart_bl_flush_rx - Discard any bytes currently waiting in the RX
 * register.  Call before starting a new command exchange to clear stale
 * data.
 * -------------------------------------------------------------------------
 */
void usart_bl_flush_rx(void);

/* Note: usart_bl.c exports the BCG-style names that main_bl.c uses directly:
 *   usart_init, usart_tx_byte, usart_rx_byte, usart_rx_buf,
 *   usart_rx_ready, usart_print, usart_print_hex32, usart_print_uint32
 * The usart_bl_* declarations above are the "nice" API; the BCG names below
 * are what gets compiled into the .o and linked. */
