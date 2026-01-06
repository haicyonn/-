#ifndef WM8978_H
#define WM8978_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

bool WM8978_Probe(I2C_HandleTypeDef *hi2c);
bool WM8978_Init(void);
uint16_t WM8978_GetAddress(void);
void WM8978_SetOut1Mute(bool mute);

typedef enum
{
  WM8978_INPUT_MIC = 0,
  WM8978_INPUT_LINEIN = 1
} WM8978_InputSource;

void WM8978_SetInputSource(WM8978_InputSource source);

#endif /* WM8978_H */
