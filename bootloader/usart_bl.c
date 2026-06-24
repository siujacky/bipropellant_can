/*
 * usart_bl.c — Bare-metal USART1 debug output for hoverboard bootloader
 *
 * USART1: TX=PA9, RX=PA10
 * Clock: 8 MHz HSI
 * Baud:  115200  (BRR = 8000000 / 115200 = 69 → actual 115942, error < 0.7%)
 *
 * No HAL. Direct register access only.
 */

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Peripheral base addresses
 * ---------------------------------------------------------------------- */
#define RCC_BASE    0x40021000U
#define GPIOA_BASE  0x40010800U
#define USART1_BASE 0x40013800U

/* -------------------------------------------------------------------------
 * RCC registers
 * ---------------------------------------------------------------------- */
#define RCC_APB2ENR  (*(volatile uint32_t *)(RCC_BASE + 0x18U))

/* Bit positions in RCC_APB2ENR */
#define RCC_APB2ENR_IOPAEN   (1U << 2)   /* GPIOA clock enable   */
#define RCC_APB2ENR_USART1EN (1U << 14)  /* USART1 clock enable  */

/* -------------------------------------------------------------------------
 * GPIOA registers (STM32F1 style: CRL/CRH layout)
 * ---------------------------------------------------------------------- */
#define GPIOA_CRH  (*(volatile uint32_t *)(GPIOA_BASE + 0x04U))

/*
 * PA9  (USART1 TX) — bits [7:4] of CRH
 *   MODE=11 (50 MHz output), CNF=10 (AF push-pull) → nibble = 0xB
 * PA10 (USART1 RX) — bits [11:8] of CRH
 *   MODE=00 (input),         CNF=01 (floating)     → nibble = 0x4
 */
#define GPIOA_CRH_PA9_MASK   (0xFU << 4)
#define GPIOA_CRH_PA9_AF_PP  (0xBU << 4)   /* AF push-pull 50 MHz */
#define GPIOA_CRH_PA10_MASK  (0xFU << 8)
#define GPIOA_CRH_PA10_FLOAT (0x4U << 8)   /* Input floating      */

/* -------------------------------------------------------------------------
 * USART1 registers
 * ---------------------------------------------------------------------- */
#define USART1_SR  (*(volatile uint32_t *)(USART1_BASE + 0x00U))
#define USART1_DR  (*(volatile uint32_t *)(USART1_BASE + 0x04U))
#define USART1_BRR (*(volatile uint32_t *)(USART1_BASE + 0x08U))
#define USART1_CR1 (*(volatile uint32_t *)(USART1_BASE + 0x0CU))

/* SR bit positions */
#define USART_SR_RXNE  (1U << 5)   /* Read data register not empty */
#define USART_SR_TXE   (1U << 7)   /* Transmit data register empty */

/* CR1 bit positions */
#define USART_CR1_RE   (1U << 2)   /* Receiver enable              */
#define USART_CR1_TE   (1U << 3)   /* Transmitter enable           */
#define USART_CR1_UE   (1U << 13)  /* USART enable                 */

/* -------------------------------------------------------------------------
 * BRR value: 8 000 000 / 115 200 = 69 (integer division, no fractional)
 * Actual baud = 8 000 000 / 69 = 115 942 Hz  → error 0.65 %  (< 0.7 %)
 * ---------------------------------------------------------------------- */
#define USART1_BRR_115200_8MHZ  69U

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * usart_init — configure GPIOA pins and USART1 at 115200-8N1.
 *
 * Call once before any other usart_* function.
 */
void usart_init(void)
{
    /* 1. Enable clocks: GPIOA and USART1 on APB2 */
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /* 2. Configure PA9 (TX) as AF push-pull 50 MHz,
     *            PA10 (RX) as input floating.
     * CRH covers pins 8-15; PA9 → nibble at [7:4], PA10 → nibble at [11:8]. */
    uint32_t crh = GPIOA_CRH;
    crh &= ~(GPIOA_CRH_PA9_MASK | GPIOA_CRH_PA10_MASK);
    crh |=  (GPIOA_CRH_PA9_AF_PP | GPIOA_CRH_PA10_FLOAT);
    GPIOA_CRH = crh;

    /* 3. Set baud rate divisor */
    USART1_BRR = USART1_BRR_115200_8MHZ;

    /* 4. Enable USART, transmitter, and receiver (8N1, no interrupts) */
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

/**
 * usart_tx_byte — transmit a single byte (blocking until TXE).
 */
void usart_tx_byte(uint8_t b)
{
    while (!(USART1_SR & USART_SR_TXE))
        ;
    USART1_DR = (uint32_t)b;
}

/**
 * usart_rx_byte — receive a single byte (blocking until RXNE).
 */
uint8_t usart_rx_byte(void)
{
    while (!(USART1_SR & USART_SR_RXNE))
        ;
    return (uint8_t)(USART1_DR & 0xFFU);
}

/**
 * usart_rx_ready — non-blocking check: returns non-zero if a byte is waiting.
 */
int usart_rx_ready(void)
{
    return (USART1_SR & USART_SR_RXNE) != 0;
}

/**
 * usart_rx_buf — receive exactly n bytes with a millisecond timeout.
 *
 * The timeout loop is a simple busy-wait calibrated for ~8 MHz:
 *   1 ms ≈ 8000 iterations at single-cycle loop body on Cortex-M3/M0+.
 *   Adjust LOOPS_PER_MS if the core clock or pipeline differs.
 *
 * Returns 1 if all n bytes were received before timeout, 0 otherwise.
 */
int usart_rx_buf(uint8_t *buf, uint32_t n, uint32_t timeout_ms)
{
    /*
     * Calibration constant: at 8 MHz with a 3-instruction poll loop
     * (LDR + AND + CBZ) the inner loop runs at roughly 3 cycles each,
     * giving ~2666 k-iterations per second.  Use 2666 per ms as a
     * conservative estimate; tune empirically if needed.
     */
#define LOOPS_PER_MS 2666U

    for (uint32_t i = 0U; i < n; i++) {
        uint32_t ticks = timeout_ms * LOOPS_PER_MS;
        while (!(USART1_SR & USART_SR_RXNE)) {
            if (ticks-- == 0U) {
                return 0;   /* timeout */
            }
        }
        buf[i] = (uint8_t)(USART1_DR & 0xFFU);
    }
    return 1;

#undef LOOPS_PER_MS
}

/**
 * usart_print — transmit a null-terminated ASCII string.
 */
void usart_print(const char *s)
{
    while (*s) {
        usart_tx_byte((uint8_t)*s);
        s++;
    }
}

/**
 * usart_print_hex32 — print a 32-bit value as exactly 8 uppercase hex digits.
 */
void usart_print_hex32(uint32_t v)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    /* Print MSB first */
    for (int shift = 28; shift >= 0; shift -= 4) {
        usart_tx_byte((uint8_t)hex_chars[(v >> (unsigned)shift) & 0xFU]);
    }
}

/**
 * usart_print_uint32 — print a 32-bit unsigned integer in decimal.
 */
void usart_print_uint32(uint32_t v)
{
    char buf[10];   /* 2^32 - 1 = 4294967295 → 10 digits */
    int  pos = 0;

    if (v == 0U) {
        usart_tx_byte('0');
        return;
    }

    /* Build digits in reverse order */
    while (v > 0U) {
        buf[pos++] = (char)('0' + (v % 10U));
        v /= 10U;
    }

    /* Emit most-significant digit first */
    while (pos > 0) {
        usart_tx_byte((uint8_t)buf[--pos]);
    }
}
