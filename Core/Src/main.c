/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  *
  * Changes:
  * - Keep buzzer off (no sound)
  * - Draw full-screen RGB565 background with status/ball overlay
  * - Refactor audio/codec/input/ui/mpu into modules
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_audio.h"
#include "app_input.h"
#include "app_ui.h"
#include "bsp_lcd.h"
#include "bsp_sdram.h"
#include "mpu6050.h"
#include "wm8978.h"
#include "platform_init.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AUDIO_NOISE_TEST_LCD_OFF  1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
bool g_wm_ready = false;
bool g_wm_ok = false;
uint16_t g_wm_addr = 0;
bool g_mpu_ok = false;
int16_t g_ax_off = 0;
int16_t g_ay_off = 0;
uint16_t g_ball_x = 0;
uint16_t g_ball_y = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
static void UART_Send(const char *msg);
static void Audio_ClockConfig(void);
static bool EnsureAudioDmaStarted(void);
static void MaybeStopAudioDma(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define LED_OFF()   HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_SET)
#define BEEP_OFF()  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET)

static void UART_Send(const char *msg)
{
  if (msg == NULL)
  {
    return;
  }
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)strlen(msg), 100);
}

static void Audio_ClockConfig(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
  PeriphClkInitStruct.PLLI2S.PLLI2SN = 192;
  PeriphClkInitStruct.PLLI2S.PLLI2SR = 2;
  PeriphClkInitStruct.PLLI2S.PLLI2SQ = 2;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

#if defined(__CC_ARM)
#pragma diag_suppress 177
#endif
static bool EnsureAudioDmaStarted(void)
{
  if (Audio_IsDmaRunning())
  {
    return true;
  }

  if (!Audio_StartTxRxDMA())
  {
    UART_Send("I2S TxRx DMA start FAIL\r\n");
    return false;
  }

  UART_Send("I2S TxRx DMA START\r\n");
  return true;
}

static void MaybeStopAudioDma(void)
{
  if (!Audio_IsRecording() && !Audio_IsPlaying())
  {
    if (Audio_StopTxRxDMA())
    {
      UART_Send("I2S TxRx DMA STOP\r\n");
    }
  }
}
#if defined(__CC_ARM)
#pragma diag_default 177
#endif
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  platform_init_mcu_infrastructure();

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  Audio_ClockConfig();
  MX_I2S2_Init();
  Audio_Init(&hi2s2);
  /* USER CODE BEGIN 2 */
  g_wm_ready = WM8978_Probe(&hi2c1);
  g_wm_ok = g_wm_ready && WM8978_Init();
  g_wm_addr = WM8978_GetAddress();

  if (g_wm_ready)
  {
    UART_Send("WM8978: I2C OK\r\n");
    if (g_wm_addr == (0x1AU << 1))
    {
      UART_Send("WM8978 addr: 0x1A\r\n");
    }
    else
    {
      UART_Send("WM8978 addr: 0x1B\r\n");
    }
  }
  else
  {
    UART_Send("WM8978: I2C FAIL\r\n");
  }

  if (g_wm_ready)
  {
    if (g_wm_ok)
    {
      UART_Send("WM8978 init OK\r\n");
    }
    else
    {
      UART_Send("WM8978 init FAIL\r\n");
    }
  }

  if (g_wm_ok)
  {
    WM8978_SetOut1Mute(true);
  }

  UART_Send("SDRAM init...\r\n");
  SDRAM_Init();
  UART_Send("SDRAM ok\r\n");
  UART_Send("LCD init...\r\n");
  LCD_Init();
  UART_Send("LCD ok\r\n");
  if (AUDIO_NOISE_TEST_LCD_OFF)
  {
    LCD_DisableOutput();
    UART_Send("LCD disabled (noise test)\r\n");
  }

  /* Make sure buzzer is off (no sound) */
  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);

  g_mpu_ok = MPU6050_Init(&hi2c1);
  g_ax_off = 0;
  g_ay_off = 0;
  if (g_mpu_ok)
  {
    MPU6050_Calibrate(&g_ax_off, &g_ay_off);
  }
  UART_Send(g_mpu_ok ? "MPU6050: OK\r\n" : "MPU6050: FAIL\r\n");

  g_ball_x = LCD_WIDTH / 2U;
  g_ball_y = LCD_HEIGHT / 2U;
  UI_DrawBall(g_ball_x, g_ball_y);
  /* USER CODE END 2 */

  /* Init scheduler */
  UART_Send("FreeRTOS init...\r\n");
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  UART_Send("FreeRTOS start...\r\n");
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
  }
  /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /* HSE + PLL -> 180MHz */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

  /* Make PLL input = 1MHz (PLLM = HSE_VALUE / 1MHz). */
#if ((HSE_VALUE % 1000000U) != 0U)
#error "HSE_VALUE must be an integer multiple of 1MHz for this PLL config. Please adjust PLLM/PLLN manually."
#endif
  RCC_OscInitStruct.PLL.PLLM = (uint32_t)(HSE_VALUE / 1000000U);
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* 180MHz needs OverDrive on STM32F429 */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
