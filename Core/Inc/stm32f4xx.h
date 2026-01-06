#ifndef __STM32F4XX_WRAPPER_H
#define __STM32F4XX_WRAPPER_H

/*
 * Use the CMSIS device header, but prevent it from auto-including the HAL
 * headers when USE_HAL_DRIVER is defined. HAL is included explicitly elsewhere.
 */
#ifdef USE_HAL_DRIVER
#define STM32F4XX_WRAPPER_RESTORE_USE_HAL_DRIVER 1
#undef USE_HAL_DRIVER
#endif

#include "../../Drivers/CMSIS/Device/ST/STM32F4xx/Include/stm32f4xx.h"

#ifdef STM32F4XX_WRAPPER_RESTORE_USE_HAL_DRIVER
#define USE_HAL_DRIVER 1
#undef STM32F4XX_WRAPPER_RESTORE_USE_HAL_DRIVER
#endif

#ifndef STM32F4XX_USE_BSRR
#define STM32F4XX_USE_BSRR 1
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#ifndef HSE_VALUE
#define HSE_VALUE ((uint32_t)25000000)
#endif

#ifndef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT ((uint16_t)0x05000)
#endif

#ifndef HSI_VALUE
#define HSI_VALUE ((uint32_t)16000000)
#endif

#ifndef RCC_BDCR_LSEMOD
#define RCC_BDCR_LSEMOD ((uint32_t)0x00000008)
#endif

#ifdef USE_STDPERIPH_DRIVER
#include "stm32f4xx_conf.h"
#endif

#endif
