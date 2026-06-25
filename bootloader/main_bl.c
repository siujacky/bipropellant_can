/* main_bl.c — STM32F103RCTx dual-transport bootloader (MCP2515/CAN + USART1).
 *
 * Target: STM32F103RCTx — 256KB flash, 48KB RAM, 2KB erase pages (high-density).
 *
 * Flash layout (all regions aligned to 2KB hw-page boundary):
 *   Bootloader : 0x08000000 – 0x08007FFF  (32KB, 16 pages)
 *   Slot A     : 0x08008000 – 0x0801FFFF  (96KB, 48 pages)  ← primary firmware
 *   Slot B     : 0x08020000 – 0x08037FFF  (96KB, 48 pages)  ← OTA target
 *   NVRAM/cfg  : 0x08038000 – 0x0803FFFF  (32KB)
 *
 * Transport:
 *   Primary  — MCP2515 via software SPI (250kbps, 8MHz crystal)
 *   Fallback — USART1 (PA9 TX, PA10 RX)
 *
 * MCP2515 SW-SPI pins (from inc/software_spi.h):
 *   MOSI = PA2  (GPIOA CRL bits[11:8]  output PP 50MHz = 0x3)
 *   MISO = PA3  (GPIOA CRL bits[15:12] input floating  = 0x4)
 *   SCK  = PB10 (GPIOB CRH bits[11:8]  output PP 50MHz = 0x3)
 *   CS   = PB11 (GPIOB CRH bits[15:12] output PP 50MHz = 0x3)
 *   INT  = PB12 (GPIOB CRH bits[19:16] input pull-up   = 0x8, ODR[12]=1)
 *
 * MCP2515 250kbps @ 8MHz crystal: CNF1=0x00, CNF2=0x9E, CNF3=0x03
 *
 * CAN IDs:
 *   TX (bootloader → host) : 0x7DE (hello/data/ack)
 *   RX (host → bootloader) : 0x7DD (upload trigger / data)
 *   Control RX              : 0x7DC (host → bootloader control cmds)
 *   Control TX              : 0x7DB (bootloader → host control responses)
 *
 * Features:
 *   - Interactive UART menu (selectable upload address, A/B slot, NVRAM clear)
 *   - Dual-slot A/B boot with persistent NVRAM config (BL_CONFIG_ADDR)
 *   - Selectable upload target: Slot A, Slot B, or custom address
 *   - NVRAM clear (APP_NVRAM_ADDR, 32KB region; flash_erase_region erases 4KB)
 *   - OTA protocol page = 1024 bytes (128 CAN frames × 8 bytes + 1 CRC frame)
 *   - Flash page-size: protocol=1024, hw=2048 (tracked in flash_bl.c)
 *   - BKP_DR1=0xB001 → extend window to 30s
 *   - BKP_DR2=0xDFD0 → jump to ROM DFU
 *   - ROM DFU helper (VID_0483:PID_DF11 for STM32CubeProgrammer / dfu-util)
 *   - PA4 (buzzer) intentionally left untouched
 *
 * Startup sequence:
 *   1. Disable interrupts.
 *   2. Check BKP registers (enter-BL magic, DFU magic).
 *   3. Read bl_config → determine boot_slot.
 *   4. Init MCP2515 + USART1.
 *   5. Broadcast CAN advert (0x7DE) + STATUS banner (0x7DB).
 *   6. Poll both transports for poll_ms (500ms normal / 30s BKP-triggered):
 *        UART 0xAA  → uart_do_upload(SLOT_A_START, 0)
 *        UART other → uart_menu()
 *        CAN 0x7DD  → can_upload()
 *        CAN 0x7DC  → handle_ctrl_cmd()
 *        timeout    → jump_to_slot(boot_slot)
 */

#include <stdint.h>
#include "device_regs.h"
#include "mcp2515_bl.h"
#include "usart_bl.h"
#include "flash_bl.h"

/* ----------------------------------------------------------------------- */
/* Address map                                                              */
/* ----------------------------------------------------------------------- */
#define SLOT_A_START      0x08008000UL
#define SLOT_B_START      0x08020000UL
#define APP_NVRAM_ADDR    0x08038000UL
#define BL_CONFIG_ADDR    0x0803E000UL
/* APP_FLASH_START and bl_config_t are defined in device_regs.h */

/* OTA protocol page (= 128 CAN frames × 8 bytes).
 * The 2KB hw page erase is handled transparently by flash_bl.c via
 * s_last_erased_page so two consecutive 1KB writes to the same 2KB
 * hw-page only trigger one erase.                                          */
#define FLASH_PAGE_SIZE    1024U   /* OTA / protocol granularity */

/* ----------------------------------------------------------------------- */
/* CAN IDs                                                                  */
/* ----------------------------------------------------------------------- */
#define CAN_TX_ID          0x7DEU
#define CAN_RX_ID          0x7DDU
#define UART_TRIGGER       0xAAU

/* Control channel */
#define BL_CTRL_RX_ID      0x7DCU
#define BL_CTRL_TX_ID      0x7DBU

/* Control command bytes */
#define BL_CMD_BOOT          0x00U
#define BL_CMD_UPLOAD_SLOT_A 0x01U
#define BL_CMD_UPLOAD_SLOT_B 0x02U
#define BL_CMD_UPLOAD_ADDR   0x03U
#define BL_CMD_SET_BOOT_A    0x04U
#define BL_CMD_SET_BOOT_B    0x05U
#define BL_CMD_CLEAR_NVRAM   0x06U
#define BL_CMD_STATUS        0x07U
#define BL_CMD_ENTER_DFU     0x08U

/* Control response status bytes */
#define BL_STATUS_OK         0x00U
#define BL_STATUS_AUTH_FAIL  0x01U
#define BL_STATUS_INVALID    0x02U
#define BL_STATUS_FLASH_ERR  0x03U

/* BL config magic & BKP magic values */
/* BL_CONFIG_MAGIC is defined in device_regs.h — do not redefine here */
#define BKP_MAGIC_ENTER_BL   0xB001U
#define BKP_MAGIC_ENTER_DFU  0xDFD0U

/* ROM DFU base addresses */
#define ROM_BL_ADDR_F1       0x1FFFF000UL
#define ROM_BL_ADDR_F4       0x1FFF0000UL

/* BL version */
#define BL_VERSION_MAJOR     1U
#define BL_VERSION_MINOR     0U

/* Hello broadcast tuning */
#define HELLO_INTERVAL_MS    50U
#define HELLO_MAX_TRIES      10U   /* 10 × 50ms = 500ms normal window */

/* ----------------------------------------------------------------------- */
/* bl_config_t is defined in device_regs.h — included above */

/* ----------------------------------------------------------------------- */
/* Simple delay: 8 MHz HSI, ~1ms per call at -O2                           */
/* ----------------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    while (ms--) {
        volatile uint32_t c = 2667U;
        while (c--)
            ;
    }
}

/* ----------------------------------------------------------------------- */
/* CRC32 — polynomial 0x04C11DB7 (MPEG-2, no bit-reflection)               */
/* ----------------------------------------------------------------------- */
static uint32_t crc32_update(uint32_t crc, uint32_t word)
{
    crc ^= word;
    for (int i = 0; i < 32; i++)
        crc = (crc & 0x80000000UL) ? (crc << 1) ^ 0x04C11DB7UL : (crc << 1);
    return crc;
}

static uint32_t crc32_page(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i = 0;
    while (i + 3U < len) {
        uint32_t word = ((uint32_t)data[i])
                      | ((uint32_t)data[i + 1] << 8)
                      | ((uint32_t)data[i + 2] << 16)
                      | ((uint32_t)data[i + 3] << 24);
        crc = crc32_update(crc, __builtin_bswap32(word));
        i += 4U;
    }
    if (i < len) {
        uint32_t word = 0U, shift = 24U;
        while (i < len) { word |= ((uint32_t)data[i++] << shift); shift -= 8U; }
        crc = crc32_update(crc, word);
    }
    return crc;
}

/* ----------------------------------------------------------------------- */
/* BL config read/write                                                     */
/* ----------------------------------------------------------------------- */
/* Validate BL_CONFIG_ADDR against actual chip flash size to avoid bus
 * faults on smaller variants (32KB chips: max addr = 0x08007FFF).         */
static int config_addr_valid(void)
{
    uint32_t flash_end = 0x08000000UL + ((uint32_t)FLASHSIZE_REG * 1024UL);
    return (BL_CONFIG_ADDR + sizeof(bl_config_t)) <= flash_end;
}

static void bl_config_read(bl_config_t *cfg)
{
    cfg->magic     = BL_CONFIG_MAGIC;
    cfg->boot_slot = 0U;
    if (!config_addr_valid()) return;
    *cfg = *(const volatile bl_config_t *)BL_CONFIG_ADDR;
    if (cfg->magic != BL_CONFIG_MAGIC) {
        cfg->magic     = BL_CONFIG_MAGIC;
        cfg->boot_slot = 0U;
    }
}

static void bl_config_write(const bl_config_t *cfg)
{
    if (!config_addr_valid()) return;
    flash_unlock();
    flash_erase_page_any(BL_CONFIG_ADDR);
    const uint8_t *p = (const uint8_t *)cfg;
    uint32_t addr = BL_CONFIG_ADDR;
    for (uint32_t i = 0; i < sizeof(bl_config_t); i += 2U) {
        uint8_t lo = p[i];
        uint8_t hi = (i + 1U < sizeof(bl_config_t)) ? p[i + 1] : 0xFFU;
        flash_write_halfword(addr, (uint16_t)(lo | ((uint16_t)hi << 8)));
        addr += 2U;
    }
    flash_lock();
}

/* ----------------------------------------------------------------------- */
/* Jump to STM32 ROM bootloader (system memory)                            */
/*                                                                          */
/* Device will enumerate as VID_0483:PID_DF11 (recognised by               */
/* STM32CubeProgrammer and dfu-util after Zadig assigns WinUSB).           */
/*                                                                          */
/* Triggered by:                                                            */
/*   BKP_DR2 = BKP_MAGIC_ENTER_DFU (0xDFD0) set by the app before reset,  */
/*   OR by menu option [7] / BL_CMD_ENTER_DFU.                             */
/*                                                                          */
/* Sequence (per AN2606 "jump from application"):                           */
/*   1. Disable SysTick.                                                    */
/*   2. Disable + clear all NVIC IRQs.                                      */
/*   3. Reset APB clocks (USB needs a clean state).                         */
/*   4. Load SP from ROM vector[0]; verify it points into SRAM.            */
/*   5. Load PC from ROM vector[1]; verify Thumb bit; jump.               */
/* ----------------------------------------------------------------------- */
static void enter_rom_dfu(void)
{
    /* Step 1: Disable SysTick */
    SYST_CSR = 0U; SYST_RVR = 0U; SYST_CVR = 0U;

    /* Step 2: Disable all IRQs and clear pending */
    __asm volatile("cpsid i");
    NVIC_ICER0 = 0xFFFFFFFFUL;
    NVIC_ICER1 = 0xFFFFFFFFUL;
    NVIC_ICER2 = 0xFFFFFFFFUL;
    *(volatile uint32_t *)0xE000E280UL = 0xFFFFFFFFUL;
    *(volatile uint32_t *)0xE000E284UL = 0xFFFFFFFFUL;
    *(volatile uint32_t *)0xE000E288UL = 0xFFFFFFFFUL;

    /* Step 3: Reset APB clocks so USB transceiver sees a clean state */
    RCC_APB1ENR = 0U;
    RCC_APB2ENR = 0U;

    /* Step 4+5: Select ROM base by DevID (AN2606 Table 2).
     * STM32F103 high-density (0x0414): ROM at 0x1FFFF000.
     * STM32F4/F7/G4 families:          ROM at 0x1FFF0000.           */
    uint16_t dev_id = (uint16_t)(DBGMCU_IDCODE & 0x0FFFU);
    uint32_t rom_base = ROM_BL_ADDR_F1;

    if (dev_id == 0x0423U || dev_id == 0x0433U ||   /* F401xB/C, F401xD/E */
        dev_id == 0x0413U || dev_id == 0x0419U ||   /* F405/407, F42x/43x */
        dev_id == 0x0431U || dev_id == 0x0441U ||   /* F411, F412         */
        dev_id == 0x0421U || dev_id == 0x0434U ||   /* F446, F469/479     */
        dev_id == 0x0449U || dev_id == 0x0458U ||   /* F7xx               */
        dev_id == 0x0468U || dev_id == 0x0469U ||   /* G4 Cat.2/3         */
        dev_id == 0x0479U) {                         /* G4 Cat.4           */
        rom_base = ROM_BL_ADDR_F4;
    }

    uint32_t rom_sp = *(volatile uint32_t *)rom_base;
    uint32_t rom_pc = *(volatile uint32_t *)(rom_base + 4U);

    /* Sanity-check: SP must point into SRAM; PC must be Thumb and in flash */
    if ((rom_sp & 0xFF000000UL) != 0x20000000UL) return;
    if ((rom_pc & 1U) == 0U || rom_pc < 0x10000000UL) return;

    SCB_VTOR = rom_base;
    __asm volatile("msr msp, %0\n" : : "r"(rom_sp));
    __asm volatile("bx  %0\n"     : : "r"(rom_pc));
    __builtin_unreachable();
}

/* ----------------------------------------------------------------------- */
/* Jump to application at arbitrary flash address                          */
/*                                                                          */
/* SP sanity-check: must land in [0x20000000, 0x2000C000) — the 48KB SRAM */
/* window on STM32F103RCTx.  Rejects 0x00000000 / erased-flash 0xFFFFFFFF.*/
/* ----------------------------------------------------------------------- */
static void jump_to_app_at(uint32_t start)
{
    __asm volatile("cpsid i");
    NVIC_ICER0 = 0xFFFFFFFFUL;
    NVIC_ICER1 = 0xFFFFFFFFUL;
    NVIC_ICER2 = 0xFFFFFFFFUL;

    uint32_t app_sp = *(volatile uint32_t *)start;
    uint32_t app_pc = *(volatile uint32_t *)(start + 4U);

    /* Reject erased flash (0xFFFFFFFF) or null SP */
    /* SP must be anywhere in SRAM space (0x20xxxxxx); 0x2000C000 is the
     * valid top-of-stack for 48KB SRAM — use > not >= so it is accepted. */
    if ((app_sp & 0xFF000000UL) != 0x20000000UL)          return;
    if ((app_pc & 1U) == 0U || app_pc < 0x08000000UL)     return;

    SCB_VTOR = start;
    __asm volatile("msr msp, %0\nbx %1\n" : : "r"(app_sp), "r"(app_pc));
    __builtin_unreachable();
}

/* ----------------------------------------------------------------------- */
/* Global state                                                             */
/* ----------------------------------------------------------------------- */
static bl_config_t g_config;

/* Upload target — default Slot A, changeable via control channel */
static uint32_t g_upload_target      = SLOT_A_START;
static uint8_t  g_upload_target_slot = 0U;   /* 0=A, 1=B, 0xFF=custom */

static void jump_to_slot(uint32_t slot)
{
    uint32_t target = (slot == 1U) ? SLOT_B_START : SLOT_A_START;
    jump_to_app_at(target);
    /* If jump_to_app_at() returned (bad vector table), hang — don't recurse */
    for (;;) { __asm volatile("wfi"); }
}

/* ----------------------------------------------------------------------- */
/* Control channel: build and transmit response on BL_CTRL_TX_ID (0x7DB)  */
/*                                                                          */
/* buf layout:                                                              */
/*   [0] = cmd          (echoed)                                            */
/*   [1] = status       (BL_STATUS_*)                                       */
/*   [2] = boot_slot    (current g_config.boot_slot)                       */
/*   [3] = upload_slot  (current g_upload_target_slot)                     */
/*   [4..7] = info4     (LE, e.g. UID0 for STATUS cmd)                     */
/* ----------------------------------------------------------------------- */
static void ctrl_respond(uint8_t cmd, uint8_t status, uint32_t info4)
{
    uint8_t buf[8];
    buf[0] = cmd;
    buf[1] = status;
    buf[2] = (uint8_t)g_config.boot_slot;
    buf[3] = g_upload_target_slot;
    buf[4] = (uint8_t)(info4);
    buf[5] = (uint8_t)(info4 >> 8);
    buf[6] = (uint8_t)(info4 >> 16);
    buf[7] = (uint8_t)(info4 >> 24);
    mcp2515_bl_tx(BL_CTRL_TX_ID, buf, 8);
}

/* ----------------------------------------------------------------------- */
/* bl_broadcast_status — send 4-frame STATUS banner on BL_CTRL_TX_ID      */
/*                                                                          */
/* Frame 0 (tag 0x07): STATUS_OK, boot_slot, target_slot, UID0 LE         */
/* Frame 1 (tag 0x17): UID1 LE, UID2[0..1]                                */
/* Frame 2 (tag 0x27): UID2[2..3], ver_maj, ver_min, slot_A=0xA1,         */
/*                     slot_B=0xB2                                          */
/* Frame 3 (tag 0x37): DevID LE, RevID LE, FlashKB LE                     */
/* ----------------------------------------------------------------------- */
static void bl_broadcast_status(void)
{
    uint32_t uid0 = DESIG_UNIQUE_ID0;
    uint32_t uid1 = DESIG_UNIQUE_ID1;
    uint32_t uid2 = DESIG_UNIQUE_ID2;
    uint8_t  slot = (uint8_t)g_config.boot_slot;
    uint8_t  buf[8];

    /* Frame 0 */
    buf[0] = BL_CMD_STATUS;
    buf[1] = BL_STATUS_OK;
    buf[2] = slot;
    buf[3] = g_upload_target_slot;
    buf[4] = (uint8_t)(uid0);
    buf[5] = (uint8_t)(uid0 >> 8);
    buf[6] = (uint8_t)(uid0 >> 16);
    buf[7] = (uint8_t)(uid0 >> 24);
    mcp2515_bl_tx(BL_CTRL_TX_ID, buf, 8);

    /* Frame 1 */
    buf[0] = 0x17U;
    buf[1] = BL_STATUS_OK;
    buf[2] = (uint8_t)(uid1);
    buf[3] = (uint8_t)(uid1 >> 8);
    buf[4] = (uint8_t)(uid1 >> 16);
    buf[5] = (uint8_t)(uid1 >> 24);
    buf[6] = (uint8_t)(uid2);
    buf[7] = (uint8_t)(uid2 >> 8);
    mcp2515_bl_tx(BL_CTRL_TX_ID, buf, 8);

    /* Frame 2 */
    buf[0] = 0x27U;
    buf[1] = BL_STATUS_OK;
    buf[2] = (uint8_t)(uid2 >> 16);
    buf[3] = (uint8_t)(uid2 >> 24);
    buf[4] = BL_VERSION_MAJOR;
    buf[5] = BL_VERSION_MINOR;
    buf[6] = 0xA1U;   /* Slot A marker */
    buf[7] = 0xB2U;   /* Slot B marker */
    mcp2515_bl_tx(BL_CTRL_TX_ID, buf, 8);

    /* Frame 3: chip self-identification */
    {
        uint32_t idcode   = DBGMCU_IDCODE;
        uint16_t dev_id   = (uint16_t)(idcode & 0x0FFFU);
        uint16_t rev_id   = (uint16_t)(idcode >> 16);
        uint16_t flash_kb = FLASHSIZE_REG;
        buf[0] = 0x37U;
        buf[1] = BL_STATUS_OK;
        buf[2] = (uint8_t)(dev_id);
        buf[3] = (uint8_t)(dev_id >> 8);
        buf[4] = (uint8_t)(rev_id);
        buf[5] = (uint8_t)(rev_id >> 8);
        buf[6] = (uint8_t)(flash_kb);
        buf[7] = (uint8_t)(flash_kb >> 8);
        mcp2515_bl_tx(BL_CTRL_TX_ID, buf, 8);
    }
}

/* ----------------------------------------------------------------------- */
/* handle_ctrl_cmd — process one frame from BL_CTRL_RX_ID (0x7DC)         */
/*                                                                          */
/* Frame layout: [0]=CMD [1..3]=UID0 low 3 bytes (auth) [4..7]=args       */
/* Auth: first 3 bytes of DESIG_UNIQUE_ID0 must match.                    */
/* ----------------------------------------------------------------------- */
static void handle_ctrl_cmd(const uint8_t *data, uint8_t len)
{
    (void)len;

    uint32_t uid0 = DESIG_UNIQUE_ID0;
    if (data[1] != (uint8_t)(uid0)       ||
        data[2] != (uint8_t)(uid0 >> 8)  ||
        data[3] != (uint8_t)(uid0 >> 16)) {
        ctrl_respond(data[0], BL_STATUS_AUTH_FAIL, 0U);
        return;
    }

    switch (data[0]) {
    case BL_CMD_BOOT:
        ctrl_respond(BL_CMD_BOOT, BL_STATUS_OK, 0U);
        delay_ms(50U);
        jump_to_slot(g_config.boot_slot);
        break;

    case BL_CMD_UPLOAD_SLOT_A:
        g_upload_target      = SLOT_A_START;
        g_upload_target_slot = 0U;
        ctrl_respond(BL_CMD_UPLOAD_SLOT_A, BL_STATUS_OK, 0U);
        break;

    case BL_CMD_UPLOAD_SLOT_B:
        g_upload_target      = SLOT_B_START;
        g_upload_target_slot = 1U;
        ctrl_respond(BL_CMD_UPLOAD_SLOT_B, BL_STATUS_OK, 0U);
        break;

    case BL_CMD_UPLOAD_ADDR: {
        uint32_t addr = (uint32_t)data[4]
                      | ((uint32_t)data[5] << 8)
                      | ((uint32_t)data[6] << 16)
                      | ((uint32_t)data[7] << 24);
        g_upload_target      = addr;
        g_upload_target_slot = 0xFFU;
        ctrl_respond(BL_CMD_UPLOAD_ADDR, BL_STATUS_OK, 0U);
        break;
    }

    case BL_CMD_SET_BOOT_A:
        g_config.boot_slot = 0U;
        bl_config_write(&g_config);
        ctrl_respond(BL_CMD_SET_BOOT_A, BL_STATUS_OK, 0U);
        delay_ms(50U);
        jump_to_slot(0U);
        break;

    case BL_CMD_SET_BOOT_B:
        g_config.boot_slot = 1U;
        bl_config_write(&g_config);
        ctrl_respond(BL_CMD_SET_BOOT_B, BL_STATUS_OK, 0U);
        delay_ms(50U);
        jump_to_slot(1U);
        break;

    case BL_CMD_CLEAR_NVRAM:
        flash_unlock();
        flash_erase_region(APP_NVRAM_ADDR, APP_NVRAM_ADDR + 4096U - 1U);
        flash_lock();
        ctrl_respond(BL_CMD_CLEAR_NVRAM, BL_STATUS_OK, 0U);
        delay_ms(200U);
        jump_to_slot(g_config.boot_slot);
        break;

    case BL_CMD_STATUS:
        bl_broadcast_status();
        break;

    case BL_CMD_ENTER_DFU:
        ctrl_respond(BL_CMD_ENTER_DFU, BL_STATUS_OK, 0U);
        delay_ms(50U);
        enter_rom_dfu();
        /* If enter_rom_dfu() returned — board has no USB or ROM BL is broken */
        ctrl_respond(BL_CMD_ENTER_DFU, BL_STATUS_FLASH_ERR, 0U);
        break;

    default:
        ctrl_respond(data[0], BL_STATUS_INVALID, 0U);
        break;
    }
}

/* ----------------------------------------------------------------------- */
/* Transport state                                                          */
/* ----------------------------------------------------------------------- */
typedef enum { TRANSPORT_NONE, TRANSPORT_CAN, TRANSPORT_UART } transport_t;
static transport_t g_transport;

/* Send a single byte on whichever transport is active */
static void send_byte(uint8_t b)
{
    if (g_transport == TRANSPORT_CAN) {
        uint8_t buf[1] = { b };
        mcp2515_bl_tx(CAN_TX_ID, buf, 1U);
    } else {
        usart_tx_byte(b);
    }
}

/* Receive exactly 'want' bytes into buf; returns 1 on success, 0 on timeout.
 * CAN path: assembles from consecutive CAN_RX_ID frames; each poll loop
 *   runs for ~timeout_ms milliseconds (267 busy-polls ≈ 1ms at 8MHz).     */
static int recv_bytes(uint8_t *buf, uint32_t want, uint32_t timeout_ms)
{
    if (g_transport == TRANSPORT_CAN) {
        uint32_t got = 0U;
        while (got < want) {
            uint16_t id16;
            uint8_t  tmp[8], rlen8;
            uint32_t polls = timeout_ms * 267U;
            int found = 0;
            while (polls--) {
                if (mcp2515_bl_rx(&id16, tmp, &rlen8) && id16 == CAN_RX_ID) {
                    found = 1;
                    break;
                }
            }
            if (!found) return 0;
            uint32_t remain  = want - got;
            uint32_t take32  = ((uint32_t)rlen8 < remain) ? (uint32_t)rlen8 : remain;
            uint8_t  take    = (uint8_t)take32;
            for (uint8_t i = 0; i < take; i++)
                buf[got++] = tmp[i];
        }
        return 1;
    } else {
        return usart_rx_buf(buf, want, timeout_ms);
    }
}

/* ----------------------------------------------------------------------- */
/* Page buffer (BSS — zeroed by startup_bl.s)                              */
/* ----------------------------------------------------------------------- */
static uint8_t page_buf[FLASH_PAGE_SIZE];

/* ----------------------------------------------------------------------- */
/* uart_do_upload — UART page upload to base_addr                          */
/* force=1: use flash_write_page_any (allows bootloader region overwrite)  */
/* ----------------------------------------------------------------------- */
static void uart_do_upload(uint32_t base_addr, int force)
{
    usart_tx_byte('S');

    uint8_t pc_buf[4];
    if (!usart_rx_buf(pc_buf, 4U, 500U)) jump_to_slot(g_config.boot_slot);
    uint32_t n_pages = (uint32_t)pc_buf[0]
                     | ((uint32_t)pc_buf[1] << 8)
                     | ((uint32_t)pc_buf[2] << 16)
                     | ((uint32_t)pc_buf[3] << 24);
    if (n_pages == 0U || n_pages > 246U) jump_to_slot(g_config.boot_slot);

    flash_unlock();
    uint32_t flash_addr = base_addr;

    for (uint32_t p = 0U; p < n_pages; p++) {
        uint8_t retry = 0U;
uart_page_retry:;
        if (!usart_rx_buf(page_buf, FLASH_PAGE_SIZE, 5000U)) {
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }
        uint8_t crc_bytes[4];
        if (!usart_rx_buf(crc_bytes, 4U, 2000U)) {
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }
        uint32_t master_crc = (uint32_t)crc_bytes[0]
                            | ((uint32_t)crc_bytes[1] << 8)
                            | ((uint32_t)crc_bytes[2] << 16)
                            | ((uint32_t)crc_bytes[3] << 24);

        uint32_t computed_crc = crc32_page(page_buf, FLASH_PAGE_SIZE);
        if (computed_crc != master_crc) {
            usart_tx_byte('E');
            if (++retry < 8U) goto uart_page_retry;
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }

        if (force)
            flash_write_page_any(flash_addr, page_buf, FLASH_PAGE_SIZE);
        else
            flash_write_page(flash_addr, page_buf, FLASH_PAGE_SIZE);

        flash_addr += FLASH_PAGE_SIZE;
        usart_tx_byte('P');
    }

    flash_lock();
    usart_tx_byte('D');
    delay_ms(100U);
    jump_to_slot(g_config.boot_slot);
}

/* ----------------------------------------------------------------------- */
/* can_upload — CAN page upload                                             */
/*                                                                          */
/* Uses g_upload_target (may be set to Slot A, Slot B, or custom address   */
/* via prior BL_CMD_UPLOAD_* control commands).                             */
/*                                                                          */
/* OTA page protocol (1024 bytes per page):                                */
/*   Frames 0-127  : 8 data bytes each  → 1024 bytes                      */
/*   Frame 128     : CRC frame — [0..3] = CRC32 LE, [4..7] ignored        */
/* ----------------------------------------------------------------------- */
static void can_upload(uint32_t base_addr)
{
    (void)base_addr;   /* ignored — g_upload_target controls the destination */

    send_byte('S');

    uint8_t pc_buf[4];
    if (!recv_bytes(pc_buf, 4U, 500U)) jump_to_slot(g_config.boot_slot);
    uint32_t n_pages = (uint32_t)pc_buf[0]
                     | ((uint32_t)pc_buf[1] << 8)
                     | ((uint32_t)pc_buf[2] << 16)
                     | ((uint32_t)pc_buf[3] << 24);
    if (n_pages == 0U || n_pages > 246U) jump_to_slot(g_config.boot_slot);

    flash_unlock();
    uint32_t flash_addr = g_upload_target;

    for (uint32_t p = 0U; p < n_pages; p++) {
        uint8_t retry = 0U;
can_page_retry:;
        uint32_t byte_idx   = 0U;
        uint32_t master_crc = 0U;

        /* Receive 128 data frames + 1 CRC frame = 129 frames total.
         *
         * CRITICAL: keep 'remain' as uint32_t before comparing with rlen8
         * (uint8_t).  When byte_idx=0, (uint8_t)(1024-0) = (uint8_t)0 = 0,
         * which would copy nothing.                                          */
        for (uint32_t f = 0U; f < 129U; f++) {
            uint16_t id16;
            uint8_t  tmp[8], rlen8;
            uint32_t polls = 2000U * 267U;
            int got = 0;
            while (polls--) {
                if (mcp2515_bl_rx(&id16, tmp, &rlen8) && id16 == CAN_RX_ID) {
                    got = 1;
                    break;
                }
            }
            if (!got) { flash_lock(); jump_to_slot(g_config.boot_slot); }

            if (f == 128U) {
                /* Frame 128: CRC only — 4 bytes LE */
                master_crc = (uint32_t)tmp[0]
                           | ((uint32_t)tmp[1] << 8)
                           | ((uint32_t)tmp[2] << 16)
                           | ((uint32_t)tmp[3] << 24);
            } else {
                /* Frames 0-127: data bytes */
                /* CRITICAL: keep remain as uint32_t for the comparison.
                 * (uint8_t)(1024-0) = 0 → take=0 → copies nothing on frame 0.
                 * Fix: compare as uint32_t, then narrow only after the min(). */
                uint32_t remain  = (uint32_t)FLASH_PAGE_SIZE - byte_idx;
                uint32_t take32  = ((uint32_t)rlen8 < remain) ? (uint32_t)rlen8 : remain;
                uint8_t  take    = (uint8_t)take32;
                for (uint8_t b = 0U; b < take; b++)
                    page_buf[byte_idx++] = tmp[b];
            }
        }

        uint32_t computed_crc = crc32_page(page_buf, FLASH_PAGE_SIZE);
        if (computed_crc != master_crc) {
            send_byte('E');
            if (++retry < 8U) goto can_page_retry;
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }

        flash_write_page(flash_addr, page_buf, FLASH_PAGE_SIZE);
        flash_addr += FLASH_PAGE_SIZE;
        send_byte('P');
    }

    flash_lock();
    /* Restore default upload target */
    g_upload_target      = SLOT_A_START;
    g_upload_target_slot = 0U;
    send_byte('D');
    delay_ms(100U);
    jump_to_slot(g_config.boot_slot);
}

/* ----------------------------------------------------------------------- */
/* uart_menu — interactive UART menu                                       */
/* ----------------------------------------------------------------------- */
static uint8_t read_hex_nibble(void)
{
    uint8_t c = usart_rx_byte();
    usart_tx_byte(c);   /* echo */
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xFFU;
}

static void uart_menu(void)
{
    usart_print("\r\n");
    usart_print("===================================\r\n");
    usart_print("  biPropellant CAN Generic BL v1\r\n");
    {
        uint32_t idc      = DBGMCU_IDCODE;
        uint16_t dev_id   = (uint16_t)(idc & 0x0FFFU);
        uint16_t rev_id   = (uint16_t)(idc >> 16);
        uint16_t flash_kb = FLASHSIZE_REG;
        usart_print("  Chip DevID: 0x");
        usart_print_hex32((uint32_t)dev_id);
        usart_print("  Rev: 0x");
        usart_print_hex32((uint32_t)rev_id);
        usart_print("\r\n");
        usart_print("  Flash: ");
        usart_print_uint32((uint32_t)flash_kb);
        usart_print(" KB\r\n");
    }
    usart_print("  UID: ");
    usart_print_hex32(DESIG_UNIQUE_ID0);
    usart_print("-");
    usart_print_hex32(DESIG_UNIQUE_ID1);
    usart_print("-");
    usart_print_hex32(DESIG_UNIQUE_ID2);
    usart_print("\r\n");
    if (g_config.boot_slot == 1U) {
        usart_print("  Active slot: B (0x08020000)\r\n");
        usart_print("  Next boot:   B\r\n");
    } else {
        usart_print("  Active slot: A (0x08008000)\r\n");
        usart_print("  Next boot:   A\r\n");
    }
    usart_print("===================================\r\n");
    usart_print(" [1] Flash -> Slot A  (primary,   0x08008000)\r\n");
    usart_print(" [2] Flash -> Slot B  (secondary, 0x08020000)\r\n");
    usart_print(" [3] Flash -> custom address\r\n");
    usart_print(" [4] Set next boot: Slot A\r\n");
    usart_print(" [5] Set next boot: Slot B\r\n");
    usart_print(" [6] Clear NVRAM (settings at 0x08038000)\r\n");
    usart_print(" [7] Enter USB DFU (ROM bootloader, VID_0483:PID_DF11)\r\n");
    usart_print("     Use STM32CubeProgrammer or dfu-util on Windows/Linux\r\n");
    usart_print(" [0] Boot application\r\n");
    usart_print("===================================\r\n");
    usart_print("Choice [0-7]: ");

    uint8_t ch = usart_rx_byte();
    usart_print("\r\n");

    switch (ch) {
    case '0':
        jump_to_slot(g_config.boot_slot);
        break;

    case '1':
        usart_print("Ready. Send 0xAA to begin...\r\n");
        while (usart_rx_byte() != UART_TRIGGER)
            ;
        uart_do_upload(SLOT_A_START, 0);
        break;

    case '2':
        usart_print("Ready. Send 0xAA to begin...\r\n");
        while (usart_rx_byte() != UART_TRIGGER)
            ;
        uart_do_upload(SLOT_B_START, 0);
        usart_print("Set Slot B as next boot? (y/n): ");
        {
            uint8_t yn = usart_rx_byte();
            usart_print("\r\n");
            if (yn == 'y' || yn == 'Y') {
                g_config.boot_slot = 1U;
                bl_config_write(&g_config);
                usart_print("Next boot: Slot B\r\n");
            }
        }
        jump_to_slot(g_config.boot_slot);
        break;

    case '3': {
        usart_print("Enter 8-digit hex address: ");
        uint32_t addr = 0U;
        int valid = 1;
        for (int i = 0; i < 8; i++) {
            uint8_t nib = read_hex_nibble();
            if (nib == 0xFFU) { valid = 0; break; }
            addr = (addr << 4) | nib;
        }
        usart_print("\r\n");
        if (!valid || addr == 0U) {
            usart_print("Invalid address.\r\n");
            jump_to_slot(g_config.boot_slot);
        }
        int force = 0;
        if (addr < APP_FLASH_START) {
            usart_print("!!! BOOTLOADER REGION - overwrite bootloader? (YES/no): ");
            uint8_t c0 = usart_rx_byte();
            uint8_t c1 = usart_rx_byte();
            uint8_t c2 = usart_rx_byte();
            uint8_t c3 = usart_rx_byte();   /* CR or \n */
            (void)c3;
            usart_print("\r\n");
            if (!((c0 == 'Y' || c0 == 'y') &&
                  (c1 == 'E' || c1 == 'e') &&
                  (c2 == 'S' || c2 == 's'))) {
                usart_print("Cancelled.\r\n");
                jump_to_slot(g_config.boot_slot);
            }
            force = 1;
        }
        usart_print("Ready. Send 0xAA to begin...\r\n");
        while (usart_rx_byte() != UART_TRIGGER)
            ;
        uart_do_upload(addr, force);
        break;
    }

    case '4':
        g_config.boot_slot = 0U;
        bl_config_write(&g_config);
        usart_print("Next boot: Slot A\r\n");
        jump_to_slot(0U);
        break;

    case '5':
        g_config.boot_slot = 1U;
        bl_config_write(&g_config);
        usart_print("Next boot: Slot B\r\n");
        jump_to_slot(1U);
        break;

    case '6':
        usart_print("Erasing NVRAM at 0x08038000 (4KB)...\r\n");
        flash_unlock();
        flash_erase_region(APP_NVRAM_ADDR, APP_NVRAM_ADDR + 4096U - 1U);
        flash_lock();
        usart_print("Done. Boot and settings will reset to defaults.\r\nBooting...\r\n");
        delay_ms(500U);
        jump_to_slot(g_config.boot_slot);
        break;

    case '7':
        usart_print("Entering USB DFU mode (VID_0483:PID_DF11)...\r\n");
        usart_print("Connect USB, open STM32CubeProgrammer, select USB DFU.\r\n");
        delay_ms(200U);
        enter_rom_dfu();
        /* enter_rom_dfu() returned — board has no USB or ROM BL is broken */
        usart_print("ROM DFU unavailable on this board — using CAN/UART only.\r\n");
        jump_to_slot(g_config.boot_slot);
        break;

    default:
        usart_print("Invalid.\r\n");
        jump_to_slot(g_config.boot_slot);
        break;
    }
}

/* ----------------------------------------------------------------------- */
/* main — bootloader entry point                                            */
/* ----------------------------------------------------------------------- */
int main(void)
{
    /* SAFETY FIRST: replicate the reference firmware idle state IMMEDIATELY.
     *
     * Source: bipropellant/bipropellant-hoverboard-firmware, src/setup.c
     *   sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;   // high-side idle = LOW
     *   sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_SET;    // low-side idle  = HIGH
     *   sBreakDeadTimeConfig.OSSR/OSSI = ENABLE            // enforce idle state when MOE=0
     *
     * The CORRECT safe state is SHORT-BRAKE (all low-side FETs ON, all high-side OFF):
     *   HIGH-SIDE pins (UH/VH/WH) → OUTPUT LOW  — high-side FET off
     *   LOW-SIDE  pins (UL/VL/WL) → OUTPUT HIGH — low-side FET on → motor phases
     *                                               shorted to GND = defined braking state
     *
     * WRONG: driving LOW-SIDE pins LOW leaves phases floating. Floating inputs
     * to some gate driver ICs (bootstrap charge / leakage) can randomly activate
     * FETs → motors "get power" unpredictably when button is pressed.
     *
     * After reset, GPIO is INPUT FLOATING → gate inputs undefined → dangerous.
     * We must reach the safe state in the very first instructions before anything
     * else (before disabling interrupts, before BKP check, before MCP2515 init).
     */
    {
        /* Enable clocks: GPIOA(bit2), GPIOB(bit3), GPIOC(bit4) */
        RCC_APB2ENR |= (1U<<2)|(1U<<3)|(1U<<4);

        /* Output PP 50MHz = 0x3 in CNFy:MODEy 4-bit field */
#define PIN_OUT(gpiox, pin) do { \
    volatile uint32_t *cr = (volatile uint32_t *)((uint32_t)(gpiox) + (((pin)>=8)?4U:0U)); \
    uint32_t shift = (((pin)%8U)*4U); \
    *cr = (*cr & ~(0xFUL<<shift)) | (0x3UL<<shift); \
} while(0)
        /* BSRR upper half = BRR (reset/low), lower half = BSR (set/high) */
#define PIN_LOW(gpiox, pin)  (((GPIO_t*)(gpiox))->BSRR = (1U<<(16U+(pin))))
#define PIN_HIGH(gpiox, pin) (((GPIO_t*)(gpiox))->BSRR = (1U<<(pin)))

        /* HIGH-SIDE (UH/VH/WH) → OUTPUT LOW  (high-side FETs OFF) */
        PIN_OUT(GPIOA_BASE, 8U);  PIN_LOW(GPIOA_BASE, 8U);   /* PA8  TIM1 UH */
        PIN_OUT(GPIOA_BASE, 9U);  PIN_LOW(GPIOA_BASE, 9U);   /* PA9  TIM1 VH */
        PIN_OUT(GPIOA_BASE, 10U); PIN_LOW(GPIOA_BASE, 10U);  /* PA10 TIM1 WH */
        PIN_OUT(GPIOC_BASE, 6U);  PIN_LOW(GPIOC_BASE, 6U);   /* PC6  TIM8 UH */
        PIN_OUT(GPIOC_BASE, 7U);  PIN_LOW(GPIOC_BASE, 7U);   /* PC7  TIM8 VH */
        PIN_OUT(GPIOC_BASE, 8U);  PIN_LOW(GPIOC_BASE, 8U);   /* PC8  TIM8 WH */

        /* LOW-SIDE  (UL/VL/WL) → OUTPUT HIGH (low-side FETs ON = short brake)
         * This matches OCNIdleState=SET from the reference firmware.
         * Motor phases are shorted to GND: defined braking, no energy from battery. */
        PIN_OUT(GPIOB_BASE, 13U); PIN_HIGH(GPIOB_BASE, 13U); /* PB13 TIM1 UL */
        PIN_OUT(GPIOB_BASE, 14U); PIN_HIGH(GPIOB_BASE, 14U); /* PB14 TIM1 VL */
        PIN_OUT(GPIOB_BASE, 15U); PIN_HIGH(GPIOB_BASE, 15U); /* PB15 TIM1 WL */
        PIN_OUT(GPIOA_BASE, 7U);  PIN_HIGH(GPIOA_BASE, 7U);  /* PA7  TIM8 UL */
        PIN_OUT(GPIOB_BASE, 0U);  PIN_HIGH(GPIOB_BASE, 0U);  /* PB0  TIM8 VL */
        PIN_OUT(GPIOB_BASE, 1U);  PIN_HIGH(GPIOB_BASE, 1U);  /* PB1  TIM8 WL */

        /* OFF_PIN (PA5) HIGH — latch power supply ON */
        PIN_OUT(GPIOA_BASE, 5U); PIN_HIGH(GPIOA_BASE, 5U);

#undef PIN_OUT
#undef PIN_LOW
#undef PIN_HIGH
    }

    /* 1. Disable all interrupts immediately */
    __asm volatile("cpsid i");
    NVIC_ICER0 = 0xFFFFFFFFUL;
    NVIC_ICER1 = 0xFFFFFFFFUL;
    NVIC_ICER2 = 0xFFFFFFFFUL;

    /* 1b. Check BKP registers.
     *
     * BKP registers survive NVIC_SystemReset() (backup power domain), so
     * the application can signal intent before resetting:
     *
     *   BKP->DR1 = 0xB001U;   // BKP_MAGIC_ENTER_BL → extend window to 30s
     *   NVIC_SystemReset();
     *
     * BKP_DR1 = 0xB001 → extend poll window to 30s.
     * BKP_DR2 = 0xDFD0 → jump to ROM DFU immediately.
     *
     * Enable PWR+BKP clocks and drop write-protection before accessing BKP. */
    uint32_t poll_ms = 500U;
    {
        RCC_APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
        PWR_CR |= PWR_CR_DBP;

        if (BKP_DR1 == BKP_MAGIC_ENTER_BL) {
            BKP_DR1  = 0x0000U;   /* consume */
            poll_ms  = 30000U;    /* extended window: 30s */
        }

        if (BKP_DR2 == BKP_MAGIC_ENTER_DFU) {
            BKP_DR2 = 0x0000U;   /* consume before jumping so a crash doesn't loop */
            enter_rom_dfu();
            /* If enter_rom_dfu() returned, continue normally */
        }
    }

    /* 2. Read BL config; determine boot_slot */
    bl_config_read(&g_config);

    /* 3. Init MCP2515 (SW-SPI) and USART1.
     *
     * mcp2515_bl_init() configures the SW-SPI GPIO pins and resets/configures
     * the MCP2515 at 250kbps (CNF1=0x00, CNF2=0x9E, CNF3=0x03, 8MHz crystal).
     * mcp2515_bl_enter_normal() takes the chip out of configuration mode into
     * normal (active) mode so TX/RX frames can flow.
     *
     * Note: PA4 (buzzer) is NOT touched here — intentionally left alone.    */
    mcp2515_bl_init();
    mcp2515_bl_enter_normal();
    usart_init();

    /* ----------------------------------------------------------------
     * LED init: PB2, active-HIGH (hoverboard onboard LED).
     * Reference: bipropellant/bipropellant-hoverboard-firmware
     *   defines.h: LED_PIN=GPIO_PIN_2, LED_PORT=GPIOB
     *
     * GPIOB clock already enabled (RCC_APB2ENR bit 3) from motor-safe
     * code at the top of main().  Just configure PB2 as output.
     * ---------------------------------------------------------------- */
    /* PB2: CRL bits[11:8] = 0x3 (output PP 50MHz) */
    GPIOB->CRL = (GPIOB->CRL & ~(0xFUL << 8)) | (0x3UL << 8);
    GPIOB->BSRR = (1U << 2);   /* LED ON immediately (active-high) */

    /* ----------------------------------------------------------------
     * 4. Build and broadcast CAN hello frame on CAN_TX_ID (0x7DE).
     *
     * Frame (8 bytes, standard ID):
     *   [0] = '3' (0x33)      — stm32-CANBootloader compat marker
     *   [1] = '1' (0x31)      — protocol version 1
     *   [2..5] = UID0 LE      — unique chip identifier
     *   [6] = BL_VERSION_MAJOR
     *   [7] = BL_VERSION_MINOR
     *
     * Also send the 4-frame STATUS banner on BL_CTRL_TX_ID (0x7DB) so the
     * host can identify the board and query slot state before uploading.
     * ---------------------------------------------------------------- */
    uint8_t advert[8];
    advert[0] = '3'; advert[1] = '1';
    uint32_t uid0_val = DESIG_UNIQUE_ID0;
    advert[2] = (uint8_t)(uid0_val);
    advert[3] = (uint8_t)(uid0_val >> 8);
    advert[4] = (uint8_t)(uid0_val >> 16);
    advert[5] = (uint8_t)(uid0_val >> 24);
    advert[6] = BL_VERSION_MAJOR;
    advert[7] = BL_VERSION_MINOR;

    mcp2515_bl_tx(CAN_TX_ID, advert, 8U);
    bl_broadcast_status();

    /* ----------------------------------------------------------------
     * 5. Poll both transports for poll_ms milliseconds.
     *
     * Re-broadcast hello every HELLO_INTERVAL_MS.  For the normal 500ms
     * window we cap at HELLO_MAX_TRIES (10) so we don't stall on a quiet
     * bus.  For the BKP-extended 30s window we allow up to poll_ms/50
     * hellos.
     *
     * Decision tree per millisecond iteration:
     *   a) UART byte received:
     *        0xAA             → TRANSPORT_UART, break → uart_do_upload()
     *        anything else    → TRANSPORT_UART, uart_menu() [does not return]
     *   b) CAN frame received on 0x7DD with matching UID0:
     *        → TRANSPORT_CAN, break → can_upload()
     *   c) CAN frame received on 0x7DC (control command):
     *        → handle_ctrl_cmd(); loop continues
     *   d) Timeout (no master):
     *        → jump_to_slot(boot_slot)
     *
     * LED toggles on each hello broadcast to show the bootloader is alive.
     * ---------------------------------------------------------------- */
    g_transport = TRANSPORT_NONE;
    uint8_t  rx_buf[8];
    uint8_t  rx_len8;
    uint32_t last_hello_ms = 0U;
    uint32_t hello_count   = 0U;
    uint8_t  led_state     = 0U;

    uint32_t max_hellos = (poll_ms > 500U)
                        ? (poll_ms / HELLO_INTERVAL_MS)
                        : HELLO_MAX_TRIES;

    for (uint32_t ms = 0U; ms < poll_ms && g_transport == TRANSPORT_NONE; ms++) {

        /* Re-broadcast hello and toggle LED every HELLO_INTERVAL_MS */
        if (ms - last_hello_ms >= HELLO_INTERVAL_MS) {
            mcp2515_bl_tx(CAN_TX_ID, advert, 8U);
            last_hello_ms = ms;
            hello_count++;

            /* Fast blink on PB2 (active-high) every hello interval.
             * HELLO_INTERVAL_MS = 50ms → ~10 Hz fast flash during BL window. */
            led_state ^= 1U;
            if (led_state) GPIOB->BSRR = (1U << 2);         /* LED on  */
            else            GPIOB->BSRR = (1U << (2 + 16));  /* LED off */

            /* In normal (non-extended) window: stop after max_hellos */
            if (hello_count >= max_hellos && poll_ms <= 500U) {
                break;
            }
        }

        /* Check UART */
        if (usart_rx_ready()) {
            uint8_t uart_byte = usart_rx_byte();
            if (uart_byte == UART_TRIGGER) {
                g_transport = TRANSPORT_UART;
            } else {
                /* Any non-trigger byte → interactive menu (does not return) */
                g_transport = TRANSPORT_UART;
                uart_menu();
            }
            break;
        }

        /* Check MCP2515 CAN */
        {
            uint16_t id16;
            if (mcp2515_bl_rx(&id16, rx_buf, &rx_len8)) {
                if (id16 == CAN_RX_ID && rx_len8 >= 4U) {
                    /* Upload trigger: host echoes UID0 back to identify target.
                     * The hello frame contains UID0 so the host always has it;
                     * using UID0 here matches what was broadcast.             */
                    uint32_t master_uid = (uint32_t)rx_buf[0]
                                        | ((uint32_t)rx_buf[1] << 8)
                                        | ((uint32_t)rx_buf[2] << 16)
                                        | ((uint32_t)rx_buf[3] << 24);
                    if (master_uid == DESIG_UNIQUE_ID0) {
                        g_transport = TRANSPORT_CAN;
                        break;
                    }
                } else if (id16 == BL_CTRL_RX_ID && rx_len8 >= 5U) {
                    /* Control command — handle in-place; continue polling */
                    handle_ctrl_cmd(rx_buf, rx_len8);
                }
            }
        }

        /* ~1ms busy delay (8MHz HSI, -O2) */
        {
            volatile uint32_t c = 2667U;
            while (c--)
                ;
        }
    }

    /* 6. Dispatch */
    if (g_transport == TRANSPORT_NONE) {
        /* No master arrived — boot based on config */
        jump_to_slot(g_config.boot_slot);
    }

    if (g_transport == TRANSPORT_CAN) {
        can_upload(SLOT_A_START);   /* base_addr ignored; g_upload_target used */
    } else {
        /* UART 0xAA backwards-compat: upload directly to Slot A */
        uart_do_upload(SLOT_A_START, 0);
    }

    return 0;   /* unreachable */
}
