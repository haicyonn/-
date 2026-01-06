#include "bsp_lcd.h"

LTDC_HandleTypeDef hltdc;

/* LTDC GPIO Configuration */
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  /* Enable Periph Clocks */
  __HAL_RCC_LTDC_CLK_ENABLE();
  __HAL_RCC_DMA2D_CLK_ENABLE();
  
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  
  /* Common config */
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;

  /* GPIOA */
  GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_11 | GPIO_PIN_12; // B5, R4, R5
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  
  /* GPIOB */
  /* R3, R6 -> AF9 */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1; 
  GPIO_InitStruct.Alternate = GPIO_AF9_LTDC; 
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* B6, B7 -> AF14 */
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9; 
  GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  
  /* GPIOC */
  GPIO_InitStruct.Pin = GPIO_PIN_7; // G6
  GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* GPIOD */
  GPIO_InitStruct.Pin = GPIO_PIN_6; // B2
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
  
  /* GPIOE */
  GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6; // B0, G0, G1
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* GPIOF */
  GPIO_InitStruct.Pin = GPIO_PIN_10; // DE
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
  
  /* GPIOG */
  GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_11 | GPIO_PIN_12; // R7, CLK, B3, B1
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
  
  /* Demo says G3 is PG10 (AF9) */
  GPIO_InitStruct.Pin = GPIO_PIN_10; // G3
  GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
  
  /* GPIOH */
  GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_8 | GPIO_PIN_13 | GPIO_PIN_15; // R0, R1, R2, G2, G4
  GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);
  
  /* GPIOI */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_9 | GPIO_PIN_10; // G5, G7, B4, VSYNC, HSYNC
  HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

  /* BL and DISP pins (Normal GPIO) */
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = 0;
  
  GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_7; // DISP, BL
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
  
  /* Enable Display and Backlight */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);
}

void LCD_DisableOutput(void)
{
  /* Disable Display and Backlight */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_RESET);
}


void LCD_Init(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /* Configure PLLSAI */
  /* PLLSAI_N=420, PLLSAI_R=3.  PLLSAI_VCO = 1MHz * 420 = 420MHz. R Output = 140MHz. */
  /* LCD Clock = 140 / 4 = 35 MHz. */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  PeriphClkInitStruct.PLLSAI.PLLSAIN = 420;
  PeriphClkInitStruct.PLLSAI.PLLSAIR = 3;
  PeriphClkInitStruct.PLLSAIDivR = RCC_PLLSAIDIVR_4;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

  hltdc.Instance = LTDC;
  
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  
  hltdc.Init.HorizontalSync     = 1 - 1;
  hltdc.Init.VerticalSync       = 1 - 1;
  hltdc.Init.AccumulatedHBP     = 1 + 46 - 1;
  hltdc.Init.AccumulatedVBP     = 1 + 23 - 1;
  hltdc.Init.AccumulatedActiveW = 1 + 46 + 800 - 1;
  hltdc.Init.AccumulatedActiveH = 1 + 23 + 480 - 1;
  hltdc.Init.TotalWidth         = 1 + 46 + 800 + 22 - 1;
  hltdc.Init.TotalHeigh         = 1 + 23 + 480 + 22 - 1;
  
  hltdc.Init.Backcolor.Blue  = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red   = 0;
  
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
     while(1);
  }

  /* Layer1 Configuration */
  LTDC_LayerCfgTypeDef pLayerCfg = {0};
  
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 800;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 480;
  
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  
  pLayerCfg.FBStartAdress = LCD_FRAME_BUFFER;
  pLayerCfg.ImageWidth = 800;
  pLayerCfg.ImageHeight = 480;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    while(1);
  }
}

void LCD_Clear(uint16_t Color)
{
  /* Simple fill using CPU */
  uint32_t index = 0;
  uint32_t total_pixels = LCD_WIDTH * LCD_HEIGHT;
  uint16_t *fb = (uint16_t*)LCD_FRAME_BUFFER;
  
  for(index = 0; index < total_pixels; index++)
  {
    fb[index] = Color;
  }
}

void LCD_DrawRect(uint16_t Xpos, uint16_t Ypos, uint16_t Width, uint16_t Height, uint16_t Color)
{
  uint16_t x, y;
  uint16_t *fb = (uint16_t*)LCD_FRAME_BUFFER;
  
  /* Top and Bottom lines */
  for(x = Xpos; x < Xpos + Width; x++)
  {
    fb[Ypos * LCD_WIDTH + x] = Color;
    fb[(Ypos + Height - 1) * LCD_WIDTH + x] = Color;
  }
  
  /* Left and Right lines */
  for(y = Ypos; y < Ypos + Height; y++)
  {
    fb[y * LCD_WIDTH + Xpos] = Color;
    fb[y * LCD_WIDTH + Xpos + Width - 1] = Color;
  }
}

void LCD_FillRect(uint16_t Xpos, uint16_t Ypos, uint16_t Width, uint16_t Height, uint16_t Color)
{
  uint16_t x, y;
  uint16_t *fb = (uint16_t*)LCD_FRAME_BUFFER;

  for(y = Ypos; y < Ypos + Height; y++)
  {
    for(x = Xpos; x < Xpos + Width; x++)
    {
      fb[y * LCD_WIDTH + x] = Color;
    }
  }
}
