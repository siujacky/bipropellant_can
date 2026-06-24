#pragma once
#include <stdint.h>

#define APP_FLASH_START     0x08008000UL
#define FLASH_HW_PAGE_SIZE  2048U
#define FLASH_PAGE_SIZE     1024U

void flash_unlock(void);
void flash_lock(void);
void flash_erase_page(uint32_t addr);
void flash_erase_page_any(uint32_t addr);
void flash_erase_region(uint32_t start, uint32_t end);
void flash_write_halfword(uint32_t addr, uint16_t data);
void flash_write_page(uint32_t dst, const uint8_t *src, uint32_t len);
void flash_write_page_any(uint32_t dst, const uint8_t *src, uint32_t len);
