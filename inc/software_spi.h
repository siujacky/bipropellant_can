#pragma once

#include "stm32f1xx_hal.h"
#include <stdint.h>

// Software SPI Pin Configuration
// Using shared pins with USART2/USART3 for auto-detection
#define SOFT_SPI_MOSI_PORT    GPIOA
#define SOFT_SPI_MOSI_PIN     GPIO_PIN_2   // PA2 (USART2_TX)
#define SOFT_SPI_MISO_PORT    GPIOA
#define SOFT_SPI_MISO_PIN     GPIO_PIN_3   // PA3 (USART2_RX)
#define SOFT_SPI_SCK_PORT     GPIOB
#define SOFT_SPI_SCK_PIN      GPIO_PIN_10  // PB10 (USART3_TX)
#define SOFT_SPI_CS_PORT      GPIOB
#define SOFT_SPI_CS_PIN       GPIO_PIN_11  // PB11 (USART3_RX)
#define SOFT_SPI_INT_PORT     GPIOB
#define SOFT_SPI_INT_PIN      GPIO_PIN_12  // PB12 (Free pin)

// SPI Mode Configuration
#define SPI_MODE0   0  // CPOL=0, CPHA=0
#define SPI_MODE1   1  // CPOL=0, CPHA=1
#define SPI_MODE2   2  // CPOL=1, CPHA=0
#define SPI_MODE3   3  // CPOL=1, CPHA=1

// Function Prototypes
void SoftSPI_Init(void);
void SoftSPI_Deinit(void);
uint8_t SoftSPI_Transfer(uint8_t data);
void SoftSPI_TransferBuffer(uint8_t *txData, uint8_t *rxData, uint16_t length);
void SoftSPI_CS_Low(void);
void SoftSPI_CS_High(void);
void SoftSPI_SetMode(uint8_t mode);

// Inline delay for bit timing - increased for reliability
// MCP2515 max SPI clock is 10MHz, we're running much slower for safety
static inline void SoftSPI_Delay(void) {
    for (volatile int i = 0; i < 64; i++) {  // Increased from 32 to 64 for more reliable reads
        __NOP();
    }
}
