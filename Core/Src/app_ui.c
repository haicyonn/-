#include "app_ui.h"

#include <string.h>

typedef struct
{
  char c;
  uint8_t rows[7];
} Font5x7;

static const Font5x7 font5x7[] = {
  {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
  {':', {0x00,0x04,0x04,0x00,0x04,0x04,0x00}},
  {'0', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x1F}},
  {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
  {'3', {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
  {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
  {'5', {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
  {'6', {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}},
  {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
  {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
  {'9', {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
  {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
  {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
  {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
  {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
  {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
  {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
  {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
  {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
  {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
  {'J', {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}},
  {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
  {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
  {'M', {0x11,0x1B,0x15,0x11,0x11,0x11,0x11}},
  {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
  {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
  {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
  {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
  {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
  {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
  {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
  {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
  {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
  {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
  {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
};

static void LCD_BlitRGB565(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h,
                           const uint16_t *src)
{
  uint16_t *fb = (uint16_t *)LCD_FRAME_BUFFER;
  for (uint16_t row = 0; row < h; row++)
  {
    memcpy(&fb[(y + row) * LCD_WIDTH + x],
           &src[row * w],
           w * 2);
  }
}

static void LCD_DrawPixelSafe(uint16_t x, uint16_t y, uint16_t color)
{
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
  {
    return;
  }
  uint16_t *fb = (uint16_t *)LCD_FRAME_BUFFER;
  fb[(uint32_t)y * LCD_WIDTH + x] = color;
}

static void LCD_FillRectSafe(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
  if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

  uint16_t *fb = (uint16_t *)LCD_FRAME_BUFFER;
  for (uint16_t row = 0; row < h; row++)
  {
    uint32_t base = (uint32_t)(y + row) * LCD_WIDTH + x;
    for (uint16_t col = 0; col < w; col++)
    {
      fb[base + col] = color;
    }
  }
}

static const uint8_t *Font5x7_Get(char c)
{
  static const uint8_t blank[7] = {0,0,0,0,0,0,0};

  for (uint32_t i = 0; i < (sizeof(font5x7) / sizeof(font5x7[0])); i++)
  {
    if (font5x7[i].c == c)
    {
      return font5x7[i].rows;
    }
  }
  return blank;
}

static void LCD_DrawChar5x7(uint16_t x, uint16_t y, char c, uint16_t color, uint8_t scale)
{
  const uint8_t *rows = Font5x7_Get(c);

  for (uint8_t row = 0; row < 7; row++)
  {
    uint8_t bits = rows[row];
    for (uint8_t col = 0; col < 5; col++)
    {
      if (bits & (1U << (4U - col)))
      {
        uint16_t px = x + (uint16_t)col * scale;
        uint16_t py = y + (uint16_t)row * scale;
        for (uint8_t sy = 0; sy < scale; sy++)
        {
          for (uint8_t sx = 0; sx < scale; sx++)
          {
            LCD_DrawPixelSafe(px + sx, py + sy, color);
          }
        }
      }
    }
  }
}

static void LCD_DrawString5x7(uint16_t x, uint16_t y, const char *s, uint16_t color, uint8_t scale)
{
  if (s == NULL)
  {
    return;
  }

  uint16_t cx = x;
  while (*s)
  {
    LCD_DrawChar5x7(cx, y, *s, color, scale);
    cx += (uint16_t)((5U + 1U) * scale);
    s++;
  }
}

static void LCD_RestoreBackgroundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  LCD_FillRectSafe(x, y, w, h, LCD_COLOR_BLACK);
}

void UI_DrawBackground(void)
{
  LCD_Clear(LCD_COLOR_BLACK);
}

void UI_DrawStatus(bool wm_ready, bool wm_ok, uint16_t wm_addr)
{
  const uint16_t x = 10;
  const uint16_t y = 10;
  const uint8_t scale = 2;
  const uint16_t line_h = (uint16_t)(7U * scale + 4U);
  const uint16_t box_w = 260;
  const uint16_t box_h = (uint16_t)(line_h * 2U + 6U);

  LCD_FillRectSafe(x - 4, y - 4, box_w, box_h, LCD_COLOR_BLACK);

  if (wm_ready)
  {
    if (wm_ok)
    {
      LCD_DrawString5x7(x, y, "WM8978 I2C OK", LCD_COLOR_LIGHTGREEN, scale);
    }
    else
    {
      LCD_DrawString5x7(x, y, "WM8978 I2C FAIL", LCD_COLOR_YELLOW, scale);
    }
  }
  else
  {
    LCD_DrawString5x7(x, y, "WM8978 I2C FAIL", LCD_COLOR_RED, scale);
  }

  if (wm_ready)
  {
    if (wm_addr == (0x1AU << 1))
    {
      LCD_DrawString5x7(x, (uint16_t)(y + line_h), "ADDR:0X1A", LCD_COLOR_WHITE, scale);
    }
    else
    {
      LCD_DrawString5x7(x, (uint16_t)(y + line_h), "ADDR:0X1B", LCD_COLOR_WHITE, scale);
    }
  }
  else
  {
    LCD_DrawString5x7(x, (uint16_t)(y + line_h), "ADDR:----", LCD_COLOR_WHITE, scale);
  }
}

void UI_DrawBall(uint16_t x, uint16_t y)
{
  LCD_FillRectSafe(x - UI_BALL_HALF, y - UI_BALL_HALF, UI_BALL_SIZE, UI_BALL_SIZE, UI_BALL_COLOR);
}

void UI_EraseBall(uint16_t x, uint16_t y)
{
  LCD_RestoreBackgroundRect(x - UI_BALL_HALF, y - UI_BALL_HALF, UI_BALL_SIZE, UI_BALL_SIZE);
}
