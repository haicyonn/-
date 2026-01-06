#include "winter_girl_crop_rgb565.h"
#include "bsp_lcd.h"

uint16_t *winter_girl_crop_rgb565 = (uint16_t *)LCD_BACKGROUND_BUFFER;

void WinterGirlBackground_Init(void)
{
  uint16_t *bg = winter_girl_crop_rgb565;
  const uint32_t w = WINTER_GIRL_CROP_RGB565_W;
  const uint32_t h = WINTER_GIRL_CROP_RGB565_H;

  for (uint32_t y = 0; y < h; y++)
  {
    uint16_t g = (uint16_t)((y * 63U) / (h - 1U));
    for (uint32_t x = 0; x < w; x++)
    {
      uint16_t b = (uint16_t)((x * 31U) / (w - 1U));
      uint16_t color = (uint16_t)((g << 5) | b);
      bg[y * w + x] = color;
    }
  }
}
