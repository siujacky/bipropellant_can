/*
 * mcp2515_bl.c — bare-metal software SPI MCP2515 driver for hoverboard bootloader.
 * No HAL, no SPI peripheral. Polling only (CANINTE=0x00).
 *
 * Pin assignments:
 *   MOSI = PA2   GPIOA CRL bits[11:8]  output 50MHz PP = 0x3
 *   MISO = PA3   GPIOA CRL bits[15:12] input floating = 0x4
 *   SCK  = PB10  GPIOB CRH bits[11:8]  output 50MHz PP = 0x3
 *   CS   = PB11  GPIOB CRH bits[15:12] output 50MHz PP = 0x3
 *   INT  = PB12  GPIOB CRH bits[19:16] input pull-up  = 0x8, ODR[12]=1
 *
 * MCP2515 bit timing: 250 kbps @ 8 MHz crystal
 *   CNF1=0x00, CNF2=0x9E, CNF3=0x03
 */

#include "mcp2515_bl.h"
#include "device_regs.h"

/* --------------------------------------------------------------------------
 * GPIO register base addresses (STM32F1 series)
 * -------------------------------------------------------------------------- */

#define GPIOA_CRL   (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_CRH   (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_IDR   (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_ODR   (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x10))
#define GPIOA_BRR   (*(volatile uint32_t *)(GPIOA_BASE + 0x14))

#define GPIOB_CRL   (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_CRH   (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_IDR   (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_ODR   (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x10))
#define GPIOB_BRR   (*(volatile uint32_t *)(GPIOB_BASE + 0x14))


/* --------------------------------------------------------------------------
 * Pin macros — use BSRR: upper 16 bits = reset (BR), lower 16 bits = set (BS)
 * -------------------------------------------------------------------------- */

/* CS = PB11 */
#define CS_HIGH()   (GPIOB_BSRR = (1U << 11))
#define CS_LOW()    (GPIOB_BSRR = (1U << (11 + 16)))

/* SCK = PB10 */
#define SCK_HIGH()  (GPIOB_BSRR = (1U << 10))
#define SCK_LOW()   (GPIOB_BSRR = (1U << (10 + 16)))

/* MOSI = PA2 */
#define MOSI_HIGH() (GPIOA_BSRR = (1U << 2))
#define MOSI_LOW()  (GPIOA_BSRR = (1U << (2 + 16)))

/* MISO = PA3 */
#define MISO_READ() ((GPIOA_IDR >> 3) & 1U)

/* --------------------------------------------------------------------------
 * MCP2515 SPI instruction bytes
 * -------------------------------------------------------------------------- */
#define MCP_INSTR_RESET       0xC0U
#define MCP_INSTR_READ        0x03U
#define MCP_INSTR_WRITE       0x02U
#define MCP_INSTR_BIT_MODIFY  0x05U
#define MCP_INSTR_RTS_TXB0    0x81U
#define MCP_INSTR_READ_RXB0   0x90U   /* read from RXB0SIDH */
#define MCP_INSTR_READ_RXB1   0x96U   /* read from RXB1SIDH */

/* MCP2515 register addresses */
#define MCP_REG_CANSTAT   0x0EU
#define MCP_REG_CANCTRL   0x0FU
#define MCP_REG_CNF3      0x28U
#define MCP_REG_CNF2      0x29U
#define MCP_REG_CNF1      0x2AU
#define MCP_REG_CANINTE   0x2BU
#define MCP_REG_CANINTF   0x2CU
#define MCP_REG_RXB0CTRL  0x60U
#define MCP_REG_RXB1CTRL  0x70U
#define MCP_REG_TXB0CTRL  0x30U
#define MCP_REG_TXB0SIDH  0x31U

/* CANSTAT / CANCTRL operation-mode bits [7:5] */
#define MCP_MODE_NORMAL   0x00U   /* CANCTRL bits[7:5] = 000 */
#define MCP_MODE_CONFIG   0x80U   /* CANCTRL bits[7:5] = 100 (=0x04 in upper nibble) */
#define MCP_CANSTAT_MODE_MASK  0xE0U

/* CANINTF flag bits */
#define MCP_CANINTF_RX0IF 0x01U
#define MCP_CANINTF_RX1IF 0x02U

/* TXB0CTRL TXREQ bit */
#define MCP_TXREQ_BIT     0x04U

/* --------------------------------------------------------------------------
 * Internal: tiny busy-wait delay
 * 8 MHz HSI: 1 ms ≈ 8000 cycles; each iteration ≈ 3 cycles → ~2667 iter/ms
 * -------------------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    volatile uint32_t count = ms * 2667U;
    while (count--) {
        __asm volatile ("nop");
    }
}

/* --------------------------------------------------------------------------
 * gpio_init — configure all five pins; no HAL, direct register access
 * -------------------------------------------------------------------------- */
static void gpio_init(void)
{
    /* Enable GPIOA (bit2) and GPIOB (bit3) clocks */
    RCC_APB2ENR |= (1U << 2) | (1U << 3);

    /* ----- GPIOA CRL (pins 0-7) ----------------------------------------- */
    /* PA2 MOSI: output 50MHz push-pull → CNF=00, MODE=11 → 0x3 in bits[11:8]  */
    /* PA3 MISO: input floating         → CNF=01, MODE=00 → 0x4 in bits[15:12] */
    GPIOA_CRL = (GPIOA_CRL
                 & ~(0xFFU << 8))          /* clear bits[15:8] for PA2+PA3 */
                | (0x3U  << 8)             /* PA2: out PP 50MHz */
                | (0x4U  << 12);           /* PA3: input floating */

    /* ----- GPIOB CRH (pins 8-15) ---------------------------------------- */
    /* PB10 SCK:  output 50MHz PP → 0x3 in bits[11:8]  (pin10 → CRH offset 8)  */
    /* PB11 CS:   output 50MHz PP → 0x3 in bits[15:12] (pin11 → CRH offset 12) */
    /* PB12 INT:  input pull-up   → CNF=10, MODE=00 → 0x8 in bits[19:16]       */
    GPIOB_CRH = (GPIOB_CRH
                 & ~(0xFFFU << 8))         /* clear bits[19:8] for PB10-12 */
                | (0x3U << 8)              /* PB10: out PP 50MHz */
                | (0x3U << 12)             /* PB11: out PP 50MHz */
                | (0x8U << 16);            /* PB12: input with pull (ODR selects up/dn) */

    /* PB12 pull-up: set ODR bit12 */
    GPIOB_ODR |= (1U << 12);

    /* Initial idle state */
    CS_HIGH();
    SCK_LOW();
}

/* --------------------------------------------------------------------------
 * spi_xfer — software SPI mode 0 (CPOL=0, CPHA=0)
 * SCK idles LOW. Data captured on rising edge, shifted on falling edge.
 * Tiny half-cycle delay: volatile counter = 3.
 * -------------------------------------------------------------------------- */
static uint8_t spi_xfer(uint8_t byte)
{
    uint8_t rx = 0U;
    uint8_t bit;

    for (bit = 0U; bit < 8U; bit++) {
        /* MSB first */
        if (byte & 0x80U) {
            MOSI_HIGH();
        } else {
            MOSI_LOW();
        }
        byte <<= 1U;

        /* half-cycle delay */
        { volatile uint32_t c = 3U; while (c--) {} }

        SCK_HIGH();

        /* sample MISO on rising edge */
        rx = (uint8_t)((rx << 1U) | (uint8_t)MISO_READ());

        /* half-cycle delay */
        { volatile uint32_t c = 3U; while (c--) {} }

        SCK_LOW();
    }

    return rx;
}

/* --------------------------------------------------------------------------
 * MCP2515 register primitives
 * -------------------------------------------------------------------------- */
static void mcp_write_reg(uint8_t addr, uint8_t value)
{
    CS_LOW();
    spi_xfer(MCP_INSTR_WRITE);
    spi_xfer(addr);
    spi_xfer(value);
    CS_HIGH();
}

static uint8_t mcp_read_reg(uint8_t addr)
{
    uint8_t val;
    CS_LOW();
    spi_xfer(MCP_INSTR_READ);
    spi_xfer(addr);
    val = spi_xfer(0x00U);
    CS_HIGH();
    return val;
}

static void mcp_bit_modify(uint8_t addr, uint8_t mask, uint8_t data)
{
    CS_LOW();
    spi_xfer(MCP_INSTR_BIT_MODIFY);
    spi_xfer(addr);
    spi_xfer(mask);
    spi_xfer(data);
    CS_HIGH();
}

/* --------------------------------------------------------------------------
 * mcp2515_bl_init
 * -------------------------------------------------------------------------- */
void mcp2515_bl_init(void)
{
    gpio_init();

    /* Hardware reset via SPI RESET instruction */
    CS_LOW();
    spi_xfer(MCP_INSTR_RESET);
    CS_HIGH();
    delay_ms(10);

    /* MUST confirm config mode before writing CNF — registers are only
     * writable in config mode. Poll up to 10ms (after reset MCP2515 must
     * enter config mode; failure here means SPI is wired wrong). */
    {
        uint8_t i;
        for (i = 0U; i < 10U; i++) {
            uint8_t canstat = mcp_read_reg(MCP_REG_CANSTAT);
            if ((canstat & MCP_CANSTAT_MODE_MASK) == 0x80U) break;
            delay_ms(1);
        }
        /* If we never reached config mode the CNF writes below will be
         * silently ignored — the init will fail and enter_normal() returns -1. */
    }

    /* Bit timing for 250 kbps @ 8 MHz crystal */
    mcp_write_reg(MCP_REG_CNF1, 0x00U);
    mcp_write_reg(MCP_REG_CNF2, 0x9EU);
    mcp_write_reg(MCP_REG_CNF3, 0x03U);

    /* RX buffer control:
     *   RXB0CTRL = 0x64: RXM=11 (accept all), BUKT=1 (roll over to RXB1)
     *   RXB1CTRL = 0x60: RXM=11 (accept all)
     */
    mcp_write_reg(MCP_REG_RXB0CTRL, 0x64U);
    mcp_write_reg(MCP_REG_RXB1CTRL, 0x60U);

    /* No interrupts — bootloader polls only */
    mcp_write_reg(MCP_REG_CANINTE, 0x00U);

    /* Clear any pending interrupt flags */
    mcp_write_reg(MCP_REG_CANINTF, 0x00U);

    /* Request normal mode: CANCTRL bits[7:5] = 000 */
    mcp_write_reg(MCP_REG_CANCTRL, MCP_MODE_NORMAL);

    /* Wait up to 10 ms for mode transition */
    {
        uint32_t i;
        for (i = 0U; i < 10U; i++) {
            uint8_t canstat = mcp_read_reg(MCP_REG_CANSTAT);
            if ((canstat & MCP_CANSTAT_MODE_MASK) == 0x00U) {
                break;
            }
            delay_ms(1);
        }
    }
}

/* --------------------------------------------------------------------------
 * mcp2515_bl_enter_normal
 * Returns 0 if CANSTAT bits[7:5]==000 (normal mode), -1 on timeout.
 * -------------------------------------------------------------------------- */
int mcp2515_bl_enter_normal(void)
{
    uint32_t i;

    mcp_write_reg(MCP_REG_CANCTRL, MCP_MODE_NORMAL);

    for (i = 0U; i < 10U; i++) {
        uint8_t canstat = mcp_read_reg(MCP_REG_CANSTAT);
        if ((canstat & MCP_CANSTAT_MODE_MASK) == 0x00U) {
            return 0;
        }
        delay_ms(1);
    }
    return -1;
}

/* --------------------------------------------------------------------------
 * mcp2515_bl_tx
 * Sends one standard-frame CAN message via TXB0.
 * Returns 1 if message was queued, 0 if TXB0 was busy.
 * -------------------------------------------------------------------------- */
int mcp2515_bl_tx(uint16_t id, const uint8_t *data, uint8_t len)
{
    uint8_t i;

    /* Check TXREQ bit of TXB0CTRL (bit2); if set, buffer is still pending */
    if (mcp_read_reg(MCP_REG_TXB0CTRL) & MCP_TXREQ_BIT) {
        return 0;
    }

    /* Clamp DLC to max 8 */
    if (len > 8U) {
        len = 8U;
    }

    /* Load TXB0 registers in a single burst starting at TXB0SIDH (0x31):
     *   TXB0SIDH = id[10:3]
     *   TXB0SIDL = id[2:0] in bits[7:5], bits[4:3]=0 (standard frame), EID17:16=0
     *   TXB0EID8 = 0x00
     *   TXB0EID0 = 0x00
     *   TXB0DLC  = len (EXIDE=0, DLC[3:0]=len)
     *   TXB0Dm   = data[0..len-1]
     */
    CS_LOW();
    spi_xfer(MCP_INSTR_WRITE);
    spi_xfer(MCP_REG_TXB0SIDH);             /* start address = 0x31 */
    spi_xfer((uint8_t)(id >> 3));            /* SIDH */
    spi_xfer((uint8_t)((id << 5) & 0xE0U)); /* SIDL — standard frame, EID=0 */
    spi_xfer(0x00U);                         /* EID8 */
    spi_xfer(0x00U);                         /* EID0 */
    spi_xfer(len & 0x0FU);                   /* DLC */
    for (i = 0U; i < len; i++) {
        spi_xfer(data[i]);
    }
    CS_HIGH();

    /* Request-to-Send TXB0 */
    CS_LOW();
    spi_xfer(MCP_INSTR_RTS_TXB0);
    CS_HIGH();

    return 1;
}

/* --------------------------------------------------------------------------
 * mcp2515_bl_rx
 * Reads one received CAN message from RXB0 (preferred) or RXB1.
 * Returns 1 if a frame was read, 0 if no frame available.
 * -------------------------------------------------------------------------- */
int mcp2515_bl_rx(uint16_t *id_out, uint8_t *data_out, uint8_t *len_out)
{
    uint8_t canintf;
    uint8_t read_cmd;
    uint8_t flag_bit;
    uint8_t rxbuf[13]; /* SIDH, SIDL, EID8, EID0, DLC, D0-D7 = 13 bytes */
    uint8_t dlc;
    uint8_t i;

    canintf = mcp_read_reg(MCP_REG_CANINTF);
    if ((canintf & (MCP_CANINTF_RX0IF | MCP_CANINTF_RX1IF)) == 0U) {
        return 0;
    }

    if (canintf & MCP_CANINTF_RX0IF) {
        read_cmd = MCP_INSTR_READ_RXB0;   /* 0x90 — reads from RXB0SIDH */
        flag_bit = MCP_CANINTF_RX0IF;
    } else {
        read_cmd = MCP_INSTR_READ_RXB1;   /* 0x96 — reads from RXB1SIDH */
        flag_bit = MCP_CANINTF_RX1IF;
    }

    /* Burst-read 13 bytes: [0]=SIDH [1]=SIDL [2]=EID8 [3]=EID0 [4]=DLC [5..12]=D0-D7 */
    CS_LOW();
    spi_xfer(read_cmd);
    for (i = 0U; i < 13U; i++) {
        rxbuf[i] = spi_xfer(0x00U);
    }
    CS_HIGH();

    /* Reconstruct standard 11-bit ID from SIDH and SIDL */
    *id_out = ((uint16_t)rxbuf[0] << 3) | ((uint16_t)rxbuf[1] >> 5);

    /* DLC — lower 4 bits only */
    dlc = rxbuf[4] & 0x0FU;
    if (dlc > 8U) {
        dlc = 8U;
    }
    *len_out = dlc;

    /* Copy payload */
    for (i = 0U; i < dlc; i++) {
        data_out[i] = rxbuf[5U + i];
    }

    /* Clear the interrupt flag for the buffer we just read */
    mcp_bit_modify(MCP_REG_CANINTF, flag_bit, 0x00U);

    return 1;
}

/* --------------------------------------------------------------------------
 * mcp2515_bl_rx_available
 * Returns non-zero if at least one RX buffer holds an unread frame.
 * -------------------------------------------------------------------------- */
int mcp2515_bl_rx_available(void)
{
    return (mcp_read_reg(MCP_REG_CANINTF) & (MCP_CANINTF_RX0IF | MCP_CANINTF_RX1IF)) != 0U;
}
