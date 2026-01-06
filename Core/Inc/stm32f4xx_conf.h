#ifndef __STM32F4xx_CONF_H
#define __STM32F4xx_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal StdPeriph config for WICED without pulling in peripheral headers. */
#ifndef assert_param
#define assert_param(expr) ((void)0)
#endif

#ifndef STM32F4XX_USE_BSRR
#define STM32F4XX_USE_BSRR 1
#endif

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

#ifdef __cplusplus
}
#endif

#endif
