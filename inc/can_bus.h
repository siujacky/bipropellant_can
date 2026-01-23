#pragma once

#include "stm32f1xx_hal.h"
#include <stdbool.h>

// Communication mode flags
extern volatile bool can_mode_active;
extern volatile bool usart_mode_active;

// Check if Software Serial (PA13/PA14) is initialized
bool CAN_IsSoftwareSerialReady(void);

// CAN bus initialization and detection
bool CAN_AutoDetectAndInit(void);
void CAN_Fallback_To_USART(void);

// CAN message processing
void CAN_ProcessMessages(void);
void CAN_SendStatus(void);
void CAN_SendBootAnnouncement(void);

// CAN ID configuration
void CAN_GetCurrentIDs(uint32_t *cmd_ids, uint32_t *status_ids);
void CAN_PrintConfiguration(void);

// CAN diagnostics and monitoring
void CAN_PrintStatistics(void);
void CAN_ResetStatistics(void);
void CAN_CheckErrors(void);

// Force console output regardless of debug_out flag
void forceLog(char *message);
