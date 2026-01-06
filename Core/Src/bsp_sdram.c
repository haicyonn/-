#include "bsp_sdram.h"

SDRAM_HandleTypeDef hsdram1;
FMC_SDRAM_TimingTypeDef SDRAM_Timing;
FMC_SDRAM_CommandTypeDef Command;

/* FMC SDRAM GPIO Configuration */
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  /* Enable FMC clock */
  __HAL_RCC_FMC_CLK_ENABLE();
  
  /* Enable GPIO clocks */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  
  /* Common GPIO configuration */
  GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  /* GPIOC configuration */
  GPIO_InitStruct.Pin = GPIO_PIN_0; // SDNWE
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* GPIOD configuration */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                        GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15; // D2, D3, D13, D14, D15, D0, D1
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* GPIOE configuration */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
                        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15; // NBL0, NBL1, D4-D12
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* GPIOF configuration */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
                        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15; // A0-A5, SDNRAS, A6-A9
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /* GPIOG configuration */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
                        GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15; // A10-A12, BA0, BA1, SDCLK, SDNCAS
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* GPIOH configuration */
  GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7; // SDNE1, SDCKE1
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);
}

void SDRAM_Init(void)
{
  /* SDRAM device configuration */
  hsdram1.Instance = FMC_SDRAM_DEVICE;

  /* Timing configuration */
  SDRAM_Timing.LoadToActiveDelay    = 2;
  SDRAM_Timing.ExitSelfRefreshDelay = 7;
  SDRAM_Timing.SelfRefreshTime      = 4;
  SDRAM_Timing.RowCycleDelay        = 7;
  SDRAM_Timing.WriteRecoveryTime    = 2;
  SDRAM_Timing.RPDelay              = 2;
  SDRAM_Timing.RCDDelay             = 2;

  hsdram1.Init.SDBank             = FMC_SDRAM_BANK2;
  hsdram1.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;
  hsdram1.Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_2;
  hsdram1.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_0;

  /* Initialize the SDRAM controller */
  if(HAL_SDRAM_Init(&hsdram1, &SDRAM_Timing) != HAL_OK)
  {
    /* Initialization Error */
    while(1);
  }

  /* Program the SDRAM external device */
  /* Step 1: Configure a clock configuration enable command */
  Command.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = 0;
  HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);

  /* Step 2: Insert 100 us minimum delay */
  HAL_Delay(1);

  /* Step 3: Configure a PALL (precharge all) command */
  Command.CommandMode            = FMC_SDRAM_CMD_PALL;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = 0;
  HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);

  /* Step 4: Configure an Auto Refresh command */
  Command.CommandMode            = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
  Command.AutoRefreshNumber      = 2;
  Command.ModeRegisterDefinition = 0;
  HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);

  /* Step 5: Program the external memory mode register */
  /* Burst Length: 4, Burst Type: Sequential, CAS Latency: 2, Write Burst Mode: Single */
  /* 
     BURST_LENGTH_4           0x0002
     BURST_TYPE_SEQUENTIAL    0x0000
     CAS_LATENCY_2            0x0020
     OPERATING_MODE_STANDARD  0x0000
     WRITEBURST_MODE_SINGLE   0x0200
  */
  uint32_t tmpmrd = 0x0002 | 0x0000 | 0x0020 | 0x0000 | 0x0200;

  Command.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = tmpmrd;
  HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);

  /* Step 6: Set the refresh rate counter */
  /* (7.81 us x Freq) - 20 */
  /* Freq = 90MHz. 7.81 * 90 = 702.9 - 20 = 683 */
  HAL_SDRAM_ProgramRefreshRate(&hsdram1, 683);
}
