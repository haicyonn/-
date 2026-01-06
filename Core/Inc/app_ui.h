#ifndef APP_UI_H
#define APP_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp_lcd.h"

#define UI_BALL_SIZE 14U
#define UI_BALL_HALF (UI_BALL_SIZE / 2U)
#define UI_BALL_COLOR LCD_COLOR_RED

void UI_DrawBackground(void);
void UI_DrawStatus(bool wm_ready, bool wm_ok, uint16_t wm_addr);
void UI_DrawBall(uint16_t x, uint16_t y);
void UI_EraseBall(uint16_t x, uint16_t y);

#endif /* APP_UI_H */
