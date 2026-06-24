#include "software_spi.h"

static uint8_t spi_mode = SPI_MODE0;

void SoftSPI_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* With CONTROL_TYPE=9 (CAN_BUS), USART2/3 are not initialized in interrupt
     * mode, so there is no USART AF conflict on PA2/PA3/PB10/PB11.
     * SPI pins are safe to reconfigure directly. */

    // Enable GPIO clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // Configure MOSI pin (Output)
    GPIO_InitStruct.Pin = SOFT_SPI_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SOFT_SPI_MOSI_PORT, &GPIO_InitStruct);
    
    // Configure MISO pin (Input) - no pull (external circuit must handle)
    GPIO_InitStruct.Pin = SOFT_SPI_MISO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SOFT_SPI_MISO_PORT, &GPIO_InitStruct);
    
    // Configure SCK pin (Output)
    GPIO_InitStruct.Pin = SOFT_SPI_SCK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SOFT_SPI_SCK_PORT, &GPIO_InitStruct);
    
    // Configure CS pin (Output)
    GPIO_InitStruct.Pin = SOFT_SPI_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SOFT_SPI_CS_PORT, &GPIO_InitStruct);
    
    // Configure INT pin (Input with interrupt capability)
    GPIO_InitStruct.Pin = SOFT_SPI_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SOFT_SPI_INT_PORT, &GPIO_InitStruct);
    
    // Set default pin states
    HAL_GPIO_WritePin(SOFT_SPI_CS_PORT, SOFT_SPI_CS_PIN, GPIO_PIN_SET);   // CS high (inactive)
    HAL_GPIO_WritePin(SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN, GPIO_PIN_RESET); // SCK low
    HAL_GPIO_WritePin(SOFT_SPI_MOSI_PORT, SOFT_SPI_MOSI_PIN, GPIO_PIN_RESET);
    
    spi_mode = SPI_MODE0;
}

void SoftSPI_Deinit(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Reconfigure all pins as analog input (low power)
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    
    GPIO_InitStruct.Pin = SOFT_SPI_MOSI_PIN;
    HAL_GPIO_Init(SOFT_SPI_MOSI_PORT, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = SOFT_SPI_MISO_PIN;
    HAL_GPIO_Init(SOFT_SPI_MISO_PORT, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = SOFT_SPI_SCK_PIN;
    HAL_GPIO_Init(SOFT_SPI_SCK_PORT, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = SOFT_SPI_CS_PIN;
    HAL_GPIO_Init(SOFT_SPI_CS_PORT, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = SOFT_SPI_INT_PIN;
    HAL_GPIO_Init(SOFT_SPI_INT_PORT, &GPIO_InitStruct);
}

void SoftSPI_SetMode(uint8_t mode) {
    spi_mode = mode & 0x03;
    
    // Set clock idle state based on mode
    if (spi_mode == SPI_MODE2 || spi_mode == SPI_MODE3) {
        HAL_GPIO_WritePin(SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN, GPIO_PIN_RESET);
    }
}

void SoftSPI_CS_Low(void) {
    SOFT_SPI_CS_PORT->BRR = SOFT_SPI_CS_PIN;  // Direct register: faster
    // MCP2515 needs 100ns CS setup time before first clock
    SoftSPI_Delay();
}

void SoftSPI_CS_High(void) {
    SOFT_SPI_CS_PORT->BSRR = SOFT_SPI_CS_PIN;  // Direct register: faster
}

// SPI Mode 0 transfer - v1.24: Sample MISO just before falling edge
// v1.22 showed first 4 bits correct, last 4 wrong = timing drift
__attribute__((optimize("-O3")))
uint8_t SoftSPI_Transfer(uint8_t data) {
    uint8_t received = 0;

    // SPI Mode 0: CPOL=0, CPHA=0
    // MCP2515 outputs on falling edge, we sample on rising (or during high)
    // Try sampling LATE in the clock high period for maximum stability

    for (int i = 7; i >= 0; i--) {
        // 1. Set MOSI bit
        if (data & (1 << i)) {
            SOFT_SPI_MOSI_PORT->BSRR = SOFT_SPI_MOSI_PIN;
        } else {
            SOFT_SPI_MOSI_PORT->BRR = SOFT_SPI_MOSI_PIN;
        }

        // 2. MOSI setup time
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();

        // 3. RISING EDGE
        SOFT_SPI_SCK_PORT->BSRR = SOFT_SPI_SCK_PIN;

        // 4. Hold clock high - let signal stabilize
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();

        // 5. Sample MISO NOW (late in high period, just before falling)
        if (SOFT_SPI_MISO_PORT->IDR & SOFT_SPI_MISO_PIN) {
            received |= (1 << i);
        }

        // 6. FALLING EDGE - MCP2515 shifts out next bit
        SOFT_SPI_SCK_PORT->BRR = SOFT_SPI_SCK_PIN;

        // 7. Wait for data valid (tV=45ns) + margin
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
    }

    return received;
}

void SoftSPI_TransferBuffer(uint8_t *txData, uint8_t *rxData, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        rxData[i] = SoftSPI_Transfer(txData[i]);
    }
}
