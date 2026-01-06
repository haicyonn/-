#ifndef __BSP_SDRAM_H
#define __BSP_SDRAM_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f4xx_hal.h"

#define SDRAM_BANK_ADDR     ((uint32_t)0xD0000000)

void SDRAM_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDRAM_H */
