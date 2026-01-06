#pragma once
#include <stdint.h>

#define WINTER_GIRL_CROP_RGB565_W 800
#define WINTER_GIRL_CROP_RGB565_H 480
#define WINTER_GIRL_CROP_RGB565_PIXELS (WINTER_GIRL_CROP_RGB565_W * WINTER_GIRL_CROP_RGB565_H)

extern uint16_t *winter_girl_crop_rgb565;

void WinterGirlBackground_Init(void);
