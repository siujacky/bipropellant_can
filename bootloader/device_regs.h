#pragma once

#include <stdint.h>

/* =========================================================================
 * device_regs.h — bare-metal register definitions for STM32F103xE
 * (256 KB flash, high-density).  No HAL dependency.
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * 1. RCC
 * ------------------------------------------------------------------------- */
#define RCC_BASE            0x40021000UL

#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x18U))
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x1CU))

#define RCC_APB1ENR_BKPEN   (1U << 27)
#define RCC_APB1ENR_PWREN   (1U << 28)

#define RCC_APB2ENR_IOPAEN  (1U << 2)
#define RCC_APB2ENR_IOPBEN  (1U << 3)

/* -------------------------------------------------------------------------
 * 2. GPIO
 * ------------------------------------------------------------------------- */
#define GPIOA_BASE          0x40010800UL
#define GPIOB_BASE          0x40010C00UL
#define GPIOC_BASE          0x40011000UL

typedef struct {
    volatile uint32_t CRL;
    volatile uint32_t CRH;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t BRR;
    volatile uint32_t LCKR;
} GPIO_t;

#define GPIOA               ((GPIO_t *)GPIOA_BASE)
#define GPIOB               ((GPIO_t *)GPIOB_BASE)
#define GPIOC               ((GPIO_t *)GPIOC_BASE)

/* -------------------------------------------------------------------------
 * 3. Flash interface
 * ------------------------------------------------------------------------- */
#define FLASH_BASE_ADDR     0x40022000UL

typedef struct {
    volatile uint32_t ACR;
    volatile uint32_t KEYR;
    volatile uint32_t OPTKEYR;
    volatile uint32_t SR;
    volatile uint32_t CR;
    volatile uint32_t AR;
    volatile uint32_t RESERVED;
    volatile uint32_t OBR;
    volatile uint32_t WRPR;
} FLASH_t;

#define FLASH               ((FLASH_t *)FLASH_BASE_ADDR)

/* Flat register macros used by flash_bl.c (same addresses, different access style) */
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x0CUL))
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x10UL))
#define FLASH_AR            (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x14UL))
#define FLASH_KEYR          (*(volatile uint32_t *)(FLASH_BASE_ADDR + 0x04UL))

/* Flash SR bits */
#define FLASH_SR_BSY        (1U << 0)
#define FLASH_SR_EOP        (1U << 5)

/* Flash CR bits */
#define FLASH_CR_PG         (1U << 0)
#define FLASH_CR_PER        (1U << 1)
#define FLASH_CR_STRT       (1U << 6)
#define FLASH_CR_LOCK       (1U << 7)

/* Unlock keys */
#define FLASH_KEY1          0x45670123UL
#define FLASH_KEY2          0xCDEF89ABUL

/* Page sizes:
 *   FLASH_HW_PAGE_SIZE — physical erase granularity for high-density devices (2 KB).
 *   FLASH_PAGE_SIZE    — OTA protocol page size (1 KB). */
#define FLASH_HW_PAGE_SIZE  2048U
#define FLASH_PAGE_SIZE     1024U

/* -------------------------------------------------------------------------
 * 4. SCB — System Control Block
 * ------------------------------------------------------------------------- */
#define SCB_VTOR            (*(volatile uint32_t *)0xE000ED08UL)

/* -------------------------------------------------------------------------
 * 5. NVIC — interrupt clear-enable registers
 * ------------------------------------------------------------------------- */
#define NVIC_ICER0          (*(volatile uint32_t *)0xE000E180UL)
#define NVIC_ICER1          (*(volatile uint32_t *)0xE000E184UL)
#define NVIC_ICER2          (*(volatile uint32_t *)0xE000E188UL)

/* -------------------------------------------------------------------------
 * 6. Backup registers (BKP) and power control (PWR)
 * ------------------------------------------------------------------------- */
#define BKP_BASE            0x40006C00UL

/* BKP_DR1 is a 16-bit register at offset 0x04 */
#define BKP_DR1             (*(volatile uint16_t *)(BKP_BASE + 0x04U))

#define PWR_CR              (*(volatile uint32_t *)0x40007000UL)
#define PWR_CR_DBP          (1U << 8)

/* BKP_DR2 — used for ROM-DFU trigger */
#define BKP_DR2             (*(volatile uint16_t *)(BKP_BASE + 0x08U))

/* Magic values */
#define BKP_MAGIC_ENTER_BL  0xB001U   /* BKP_DR1: extended BL window on next reset */
#define BKP_MAGIC_ENTER_DFU 0xDFD0U   /* BKP_DR2: jump to ROM DFU (VID_0483:PID_DF11) */

/* -------------------------------------------------------------------------
 * 7. Chip unique ID (96-bit, three 32-bit words starting at 0x1FFFF7E8)
 * ------------------------------------------------------------------------- */
#define DESIG_UNIQUE_ID0    (*(volatile uint32_t *)0x1FFFF7E8UL)
#define DESIG_UNIQUE_ID1    (*(volatile uint32_t *)0x1FFFF7ECUL)
#define DESIG_UNIQUE_ID2    (*(volatile uint32_t *)0x1FFFF7F0UL)

/* -------------------------------------------------------------------------
 * 8. Flash size register (16-bit, value in KB)
 * ------------------------------------------------------------------------- */
#define FLASHSIZE_REG       (*(volatile uint16_t *)0x1FFFF7E0UL)

/* -------------------------------------------------------------------------
 * 9. Debug MCU identification register
 * ------------------------------------------------------------------------- */
#define DBGMCU_IDCODE       (*(volatile uint32_t *)0xE0042000UL)

/* -------------------------------------------------------------------------
 * 10. Application flash guard address
 * ------------------------------------------------------------------------- */
#define APP_FLASH_START     0x08008000UL   /* Slot A start; bootloader guard */

/* -------------------------------------------------------------------------
 * 11. Slot addresses
 *
 *   Flash layout (256 KB device, 0x08000000 .. 0x0803FFFF):
 *     0x08000000 .. 0x08007FFF  Bootloader      ( 32 KB)
 *     0x08008000 .. 0x0801FFFF  Slot A          ( 96 KB)
 *     0x08020000 .. 0x08037FFF  Slot B          ( 96 KB)
 *     0x08038000 .. 0x0803DFFF  APP_NVRAM       ( 24 KB)
 *     0x0803E000 .. 0x0803FFFF  BL_CONFIG       (  8 KB)
 * ------------------------------------------------------------------------- */
#define SLOT_A_START        0x08008000UL
#define SLOT_B_START        0x08020000UL
#define APP_NVRAM_ADDR      0x08038000UL
#define BL_CONFIG_ADDR      0x0803E000UL

/* -------------------------------------------------------------------------
 * 12. SysTick and ROM bootloader addresses
 * ------------------------------------------------------------------------- */
#define SYST_CSR            (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR            (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR            (*(volatile uint32_t *)0xE000E018UL)

#define ROM_BL_ADDR_F1      0x1FFFF000UL   /* STM32F1 system-memory bootloader */
#define ROM_BL_ADDR_F4      0x1FFF0000UL   /* STM32F4 system-memory bootloader */

/* -------------------------------------------------------------------------
 * 13. Bootloader configuration record (stored at BL_CONFIG_ADDR)
 * ------------------------------------------------------------------------- */
#define BL_CONFIG_MAGIC     0xB007C0DEUL

typedef struct {
    uint32_t magic;       /* Must equal BL_CONFIG_MAGIC to be considered valid */
    uint32_t boot_slot;   /* 0 = Slot A, 1 = Slot B */
} bl_config_t;
