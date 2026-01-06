/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "app_audio.h"
#include "app_input.h"
#include "app_ui.h"
#include "app_wifi.h"
#include "bsp_lcd.h"
#include "lwip/inet.h"
#include "lwip/errno.h"
#include "lwip/sockets.h"
#include "mpu6050.h"
#include "usart.h"
#include "wm8978.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ACCEL_LSB_PER_G            16384.0f
#define BALL_GAIN                  200.0f
#define WIFI_ONLY_MODE             0
#define ENABLE_DEFAULT_TASK        1
#define ENABLE_AUDIO_TASK          1
#define ENABLE_IMU_TASK            1
#define ENABLE_UI_TASK             1
#define ENABLE_WIFI_TASK           1
#define ENABLE_AUDIO_UPLOAD_TASK   1
#define ENABLE_UI_DRAW             1
#define START_TASKS_AFTER_WIFI     0
#define APP_START_FLAG             (1U << 0U)

#define DEFAULT_TASK_STACK_BYTES   (128U * 4U)
#define AUDIO_TASK_STACK_BYTES     (2048U * 4U)
#define IMU_TASK_STACK_BYTES       (512U * 4U)
#define UI_TASK_STACK_BYTES        (1024U * 4U)
#define WIFI_TASK_STACK_BYTES      (2048U * 4U)
#define WIFI_INIT_STACK_BYTES      (2048U * 4U)
#define AUDIO_UPLOAD_TASK_STACK_BYTES (2048U * 4U)

#define AUDIO_UPLOAD_SERVER_IP        "192.168.5.3"
#define AUDIO_UPLOAD_SERVER_PORT      40000U
#define AUDIO_UDP_FRAME_MS            8U
#define AUDIO_UDP_BYTES_PER_SAMPLE    (AUDIO_SAMPLE_BITS / 8U)
#define AUDIO_UDP_FRAME_SAMPLES       ((AUDIO_SAMPLE_RATE_HZ / 1000U) * AUDIO_UDP_FRAME_MS)
#define AUDIO_UDP_FRAME_BYTES         (AUDIO_UDP_FRAME_SAMPLES * AUDIO_UDP_BYTES_PER_SAMPLE * AUDIO_SAMPLE_CHANNELS)
#define AUDIO_UDP_IDLE_DELAY_MS       5U
#define AUDIO_UDP_READ_TIMEOUT_MS     30U
#define AUDIO_UDP_LOCK_TIMEOUT_MS     50U
#define AUDIO_UDP_SEND_PAUSE_MS       2U
#define AUDIO_UPLOAD_RETRY_DELAY_MS   1000U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern bool g_wm_ready;
extern bool g_wm_ok;
extern uint16_t g_wm_addr;
extern bool g_mpu_ok;
extern int16_t g_ax_off;
extern int16_t g_ay_off;
extern uint16_t g_ball_x;
extern uint16_t g_ball_y;

osMutexId_t i2cMutexHandle;
const osMutexAttr_t i2cMutex_attributes = {
  .name = "i2cMutex"
};

osMutexId_t uartMutexHandle;
const osMutexAttr_t uartMutex_attributes = {
  .name = "uartMutex"
};

osMutexId_t lwipMutexHandle;
const osMutexAttr_t lwipMutex_attributes = {
  .name = "lwipMutex"
};

osEventFlagsId_t appStartEventHandle;
const osEventFlagsAttr_t appStartEvent_attributes = {
  .name = "appStartEvent"
};

static bool s_app_tasks_started = false;
static int s_audio_upload_last_errno = 0;
static int s_audio_udp_socket = -1;
static struct sockaddr_in s_audio_udp_addr;
static bool s_audio_udp_ready = false;
static volatile bool s_audio_udp_sending = false;
static volatile bool s_audio_silence_test = false;

static StaticTask_t defaultTaskCb;
static StackType_t defaultTaskStack[DEFAULT_TASK_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t audioTaskCb;
static StackType_t audioTaskStack[AUDIO_TASK_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t imuTaskCb;
static StackType_t imuTaskStack[IMU_TASK_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t uiTaskCb;
static StackType_t uiTaskStack[UI_TASK_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t wifiTaskCb;
static StackType_t wifiTaskStack[WIFI_TASK_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t wifiInitTaskCb;
static StackType_t wifiInitTaskStack[WIFI_INIT_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t audioUploadTaskCb;
static StackType_t audioUploadTaskStack[AUDIO_UPLOAD_TASK_STACK_BYTES / sizeof(StackType_t)];

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_mem = &defaultTaskStack[0],
  .stack_size = DEFAULT_TASK_STACK_BYTES,
  .cb_mem = &defaultTaskCb,
  .cb_size = sizeof(defaultTaskCb),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for audioTask */
osThreadId_t audioTaskHandle;
const osThreadAttr_t audioTask_attributes = {
  .name = "audioTask",
  .stack_mem = &audioTaskStack[0],
  .stack_size = AUDIO_TASK_STACK_BYTES,
  .cb_mem = &audioTaskCb,
  .cb_size = sizeof(audioTaskCb),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for imuTask */
osThreadId_t imuTaskHandle;
const osThreadAttr_t imuTask_attributes = {
  .name = "imuTask",
  .stack_mem = &imuTaskStack[0],
  .stack_size = IMU_TASK_STACK_BYTES,
  .cb_mem = &imuTaskCb,
  .cb_size = sizeof(imuTaskCb),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for uiTask */
osThreadId_t uiTaskHandle;
const osThreadAttr_t uiTask_attributes = {
  .name = "uiTask",
  .stack_mem = &uiTaskStack[0],
  .stack_size = UI_TASK_STACK_BYTES,
  .cb_mem = &uiTaskCb,
  .cb_size = sizeof(uiTaskCb),
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for wifiTask */
osThreadId_t wifiTaskHandle;
const osThreadAttr_t wifiTask_attributes = {
  .name = "wifiTask",
  .stack_mem = &wifiTaskStack[0],
  .stack_size = WIFI_TASK_STACK_BYTES,
  .cb_mem = &wifiTaskCb,
  .cb_size = sizeof(wifiTaskCb),
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for wifiInitTask */
osThreadId_t wifiInitTaskHandle;
const osThreadAttr_t wifiInitTask_attributes = {
  .name = "wifiInitTask",
  .stack_mem = &wifiInitTaskStack[0],
  .stack_size = WIFI_INIT_STACK_BYTES,
  .cb_mem = &wifiInitTaskCb,
  .cb_size = sizeof(wifiInitTaskCb),
  .priority = (osPriority_t) osPriorityRealtime,
};
/* Definitions for audioUploadTask */
osThreadId_t audioUploadTaskHandle;
const osThreadAttr_t audioUploadTask_attributes = {
  .name = "audioUploadTask",
  .stack_mem = &audioUploadTaskStack[0],
  .stack_size = AUDIO_UPLOAD_TASK_STACK_BYTES,
  .cb_mem = &audioUploadTaskCb,
  .cb_size = sizeof(audioUploadTaskCb),
  .priority = (osPriority_t) osPriorityAboveNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void UART_Send(const char *msg);
static bool Audio_EnsureDmaStarted(void);
static void Audio_MaybeStopDma(void);
static bool I2C_Lock(void);
static void I2C_Unlock(void);
static void EnsureInterruptsEnabled(void);
static void EnsureRtosTickRunning(void);
static void CreateAppTasks(void);
static void WaitForAppStart(void);
static void SignalAppStart(void);
static bool AudioUpload_UdpOpen(void);
static void AudioUpload_UdpClose(void);
static bool AudioUpload_UdpSend(const uint8_t *data, size_t len);
static bool AudioUpload_Lock(uint32_t timeout_ms);
static void AudioUpload_Unlock(void);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartTask04(void *argument);
void StartTask05(void *argument);
void StartTask06(void *argument);
void StartTask07(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  i2cMutexHandle = osMutexNew(&i2cMutex_attributes);
  uartMutexHandle = osMutexNew(&uartMutex_attributes);
  lwipMutexHandle = osMutexNew(&lwipMutex_attributes);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  appStartEventHandle = osEventFlagsNew(&appStartEvent_attributes);
  if (appStartEventHandle == NULL)
  {
    UART_Send("appStartEvent create failed\r\n");
  }

  /* Create the thread(s) */
#if !START_TASKS_AFTER_WIFI
  CreateAppTasks();
#endif

  /* creation of wifiInitTask */
  UART_Send("create wifiInitTask...\r\n");
  wifiInitTaskHandle = osThreadNew(StartTask06, NULL, &wifiInitTask_attributes);
  if (wifiInitTaskHandle == NULL)
  {
    UART_Send("wifiInitTask create failed\r\n");
  }

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN CreateAppTasks */
static void CreateAppTasks(void)
{
  if (s_app_tasks_started)
  {
    return;
  }
  s_app_tasks_started = true;

#if !WIFI_ONLY_MODE && ENABLE_DEFAULT_TASK
  /* creation of defaultTask */
  UART_Send("create defaultTask...\r\n");
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  if (defaultTaskHandle == NULL)
  {
    UART_Send("defaultTask create failed\r\n");
  }
#endif

#if !WIFI_ONLY_MODE && ENABLE_AUDIO_TASK
  /* creation of audioTask */
  UART_Send("create audioTask...\r\n");
  audioTaskHandle = osThreadNew(StartTask02, NULL, &audioTask_attributes);
  if (audioTaskHandle == NULL)
  {
    UART_Send("audioTask create failed\r\n");
  }
#endif

#if !WIFI_ONLY_MODE && ENABLE_IMU_TASK
  /* creation of imuTask */
  UART_Send("create imuTask...\r\n");
  imuTaskHandle = osThreadNew(StartTask03, NULL, &imuTask_attributes);
  if (imuTaskHandle == NULL)
  {
    UART_Send("imuTask create failed\r\n");
  }
#endif

#if !WIFI_ONLY_MODE && ENABLE_UI_TASK
  /* creation of uiTask */
  UART_Send("create uiTask...\r\n");
  uiTaskHandle = osThreadNew(StartTask04, NULL, &uiTask_attributes);
  if (uiTaskHandle == NULL)
  {
    UART_Send("uiTask create failed\r\n");
  }
#endif

#if !WIFI_ONLY_MODE && ENABLE_WIFI_TASK
  /* creation of wifiTask */
  UART_Send("create wifiTask...\r\n");
  wifiTaskHandle = osThreadNew(StartTask05, NULL, &wifiTask_attributes);
  if (wifiTaskHandle == NULL)
  {
    UART_Send("wifiTask create failed\r\n");
  }
#endif

#if !WIFI_ONLY_MODE && ENABLE_AUDIO_UPLOAD_TASK
  /* creation of audioUploadTask */
  UART_Send("create audioUploadTask...\r\n");
  audioUploadTaskHandle = osThreadNew(StartTask07, NULL, &audioUploadTask_attributes);
  if (audioUploadTaskHandle == NULL)
  {
    UART_Send("audioUploadTask create failed\r\n");
  }
#endif
}
/* USER CODE END CreateAppTasks */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  WaitForAppStart();
  UART_Send("defaultTask start\r\n");
  EnsureInterruptsEnabled();
  EnsureRtosTickRunning();
  /* Infinite loop */
  for(;;)
  {
    HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin);
    osDelay(500);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the audioTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  WaitForAppStart();
  UART_Send("audioTask start\r\n");
  osDelay(50);
  typedef enum
  {
    AUDIO_KEY2_IDLE = 0,
    AUDIO_KEY2_RECORDING,
    AUDIO_KEY2_RECORDED,
    AUDIO_KEY2_PLAYING
  } audio_key2_state_t;
  audio_key2_state_t key2_state = AUDIO_KEY2_IDLE;
  /* Infinite loop */
  for(;;)
  {
    bool key1 = Input_Key1Pressed();
    bool key2 = Input_Key2Pressed();

    if (key1)
    {
      if (!g_wm_ok)
      {
        UART_Send("KEY1 ignored (WM8978 init failed)\r\n");
      }
      else
      {
        s_audio_silence_test = !s_audio_silence_test;
        if (s_audio_silence_test)
        {
          Audio_RecordStop();
          Audio_PlayStop();
          key2_state = AUDIO_KEY2_IDLE;
          if (Audio_EnsureDmaStarted())
          {
            if (I2C_Lock())
            {
              WM8978_SetOut1Mute(false);
              I2C_Unlock();
            }
            UART_Send("SILENCE TEST ON\r\n");
          }
        }
        else
        {
          if (I2C_Lock())
          {
            WM8978_SetOut1Mute(true);
            I2C_Unlock();
          }
          UART_Send("SILENCE TEST OFF\r\n");
          Audio_MaybeStopDma();
        }
      }
    }

    if (key2)
    {
      if (s_audio_silence_test)
      {
        UART_Send("KEY2 ignored (silence test)\r\n");
      }
      else if (!g_wm_ok)
      {
        UART_Send("KEY2 press ignored (WM8978 init failed)\r\n");
      }
      else
      {
        switch (key2_state)
        {
          case AUDIO_KEY2_IDLE:
            if (Audio_EnsureDmaStarted())
            {
              if (I2C_Lock())
              {
                WM8978_SetOut1Mute(true);
                I2C_Unlock();
              }
              Audio_RecordStart();
              UART_Send("REC START (MIC)\r\n");
              key2_state = AUDIO_KEY2_RECORDING;
            }
            break;

          case AUDIO_KEY2_RECORDING:
          {
            Audio_RecordStop();
            if (I2C_Lock())
            {
              WM8978_SetOut1Mute(true);
              I2C_Unlock();
            }
            char msg[96];
            snprintf(msg, sizeof(msg), "REC STOP len=%lu peak=%d\r\n",
                     (unsigned long)Audio_GetRecordLength(),
                     (int)Audio_GetLastPeak());
            UART_Send(msg);
            key2_state = AUDIO_KEY2_RECORDED;
            break;
          }

          case AUDIO_KEY2_RECORDED:
            if (Audio_GetRecordLength() == 0U)
            {
              UART_Send("PLAY: no recorded data\r\n");
              key2_state = AUDIO_KEY2_IDLE;
            }
            else if (Audio_EnsureDmaStarted())
            {
              if (I2C_Lock())
              {
                WM8978_SetOut1Mute(false);
                I2C_Unlock();
              }
              Audio_PlayStart();
              UART_Send("PLAY START\r\n");
              key2_state = AUDIO_KEY2_PLAYING;
            }
            break;

          case AUDIO_KEY2_PLAYING:
          default:
            Audio_PlayStop();
            if (I2C_Lock())
            {
              WM8978_SetOut1Mute(true);
              I2C_Unlock();
            }
            UART_Send("PLAY STOP\r\n");
            key2_state = AUDIO_KEY2_IDLE;
            break;
        }
      }
    }

    if (key2_state == AUDIO_KEY2_RECORDING && !Audio_IsRecording())
    {
      char msg[96];
      snprintf(msg, sizeof(msg), "REC STOP (auto) len=%lu peak=%d\r\n",
               (unsigned long)Audio_GetRecordLength(),
               (int)Audio_GetLastPeak());
      UART_Send(msg);
      key2_state = AUDIO_KEY2_RECORDED;
    }

    if (key2_state == AUDIO_KEY2_PLAYING && !Audio_IsPlaying())
    {
      if (I2C_Lock())
      {
        WM8978_SetOut1Mute(true);
        I2C_Unlock();
      }
      UART_Send("PLAY STOP (auto)\r\n");
      key2_state = AUDIO_KEY2_IDLE;
    }

    if (g_wm_ok && I2C_Lock())
    {
      WM8978_SetOut1Mute(!(Audio_IsPlaying() || s_audio_silence_test));
      I2C_Unlock();
    }

    Audio_MaybeStopDma();
    osDelay(2);
  }
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
* @brief Function implementing the imuTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  WaitForAppStart();
  UART_Send("imuTask start\r\n");
  static bool mpu_warned = false;
  uint16_t prev_x = g_ball_x;
  uint16_t prev_y = g_ball_y;
  float ax_f = 0.0f;
  float ay_f = 0.0f;
  uint32_t last_status = HAL_GetTick();

  /* Infinite loop */
  for(;;)
  {
    if (!g_mpu_ok && !mpu_warned)
    {
      UART_Send("MPU6050 init failed\r\n");
      mpu_warned = true;
    }

    if (g_mpu_ok)
    {
      int16_t ax = 0;
      int16_t ay = 0;
      int16_t az = 0;
      bool ok = false;

      if (I2C_Lock())
      {
        ok = MPU6050_ReadAccelRaw(&ax, &ay, &az);
        I2C_Unlock();
      }

      if (ok)
      {
        (void)az;
        ax = (int16_t)(ax - g_ax_off);
        ay = (int16_t)(ay - g_ay_off);

        float ax_g = (float)ax / ACCEL_LSB_PER_G;
        float ay_g = (float)ay / ACCEL_LSB_PER_G;
        ax_f = ax_f * 0.9f + ax_g * 0.1f;
        ay_f = ay_f * 0.9f + ay_g * 0.1f;

        int32_t x = (int32_t)((float)(LCD_WIDTH / 2U) + ax_f * BALL_GAIN);
        int32_t y = (int32_t)((float)(LCD_HEIGHT / 2U) - ay_f * BALL_GAIN);

        if (x < (int32_t)UI_BALL_HALF) x = (int32_t)UI_BALL_HALF;
        if (x > (int32_t)(LCD_WIDTH - 1U - UI_BALL_HALF)) x = (int32_t)(LCD_WIDTH - 1U - UI_BALL_HALF);
        if (y < (int32_t)UI_BALL_HALF) y = (int32_t)UI_BALL_HALF;
        if (y > (int32_t)(LCD_HEIGHT - 1U - UI_BALL_HALF)) y = (int32_t)(LCD_HEIGHT - 1U - UI_BALL_HALF);

        g_ball_x = (uint16_t)x;
        g_ball_y = (uint16_t)y;

        UI_EraseBall(prev_x, prev_y);
        UI_DrawBall((uint16_t)x, (uint16_t)y);

        prev_x = (uint16_t)x;
        prev_y = (uint16_t)y;
      }
    }

    uint32_t now = HAL_GetTick();
    if (now - last_status >= 500U)
    {
      last_status = now;
      UI_DrawStatus(g_wm_ready, g_wm_ok, g_wm_addr);
    }

    osDelay(20);
  }
  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartTask04 */
/**
* @brief Function implementing the uiTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask04 */
void StartTask04(void *argument)
{
  /* USER CODE BEGIN StartTask04 */
  WaitForAppStart();
  UART_Send("uiTask start\r\n");
  /* Infinite loop */
  for(;;)
  {
    osDelay(100);
  }
  /* USER CODE END StartTask04 */
}

/* USER CODE BEGIN Header_StartTask05 */
/**
* @brief Function implementing the wifiTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask05 */
void StartTask05(void *argument)
{
  /* USER CODE BEGIN StartTask05 */
  (void)argument;
  WaitForAppStart();
  UART_Send("wifiTask start\r\n");
  for (;;)
  {
    if (!App_Wifi_IsConnected())
    {
      UART_Send("WiFi: not ready\r\n");
      osDelay(5000);
      continue;
    }

    if (s_audio_udp_sending || Audio_IsRecording())
    {
      osDelay(1000);
      continue;
    }

    uint32_t elapsed = 0;
    bool gw_ok = App_Wifi_PingHost("192.168.5.1", 1000, &elapsed);
    char msg[96];
    snprintf(msg, sizeof(msg),
             "Ping GW 192.168.5.1: %s (%lums)\r\n",
             gw_ok ? "OK" : "FAIL",
             (unsigned long)elapsed);
    UART_Send(msg);

    elapsed = 0;
    bool baidu_ok = App_Wifi_PingHost("baidu.com", 1500, &elapsed);
    snprintf(msg, sizeof(msg),
             "Ping baidu.com: %s (%lums)\r\n",
             baidu_ok ? "OK" : "FAIL",
             (unsigned long)elapsed);
    UART_Send(msg);

    elapsed = 0;
    bool pc_ok = App_Wifi_PingHost(AUDIO_UPLOAD_SERVER_IP, 1000, &elapsed);
    snprintf(msg, sizeof(msg),
             "Ping PC %s: %s (%lums)\r\n",
             AUDIO_UPLOAD_SERVER_IP,
             pc_ok ? "OK" : "FAIL",
             (unsigned long)elapsed);
    UART_Send(msg);

    osDelay(60000);
  }
  /* USER CODE END StartTask05 */
}

/* USER CODE BEGIN Header_StartTask07 */
/**
* @brief Function implementing the audioUploadTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask07 */
void StartTask07(void *argument)
{
  /* USER CODE BEGIN StartTask07 */
  (void)argument;
  WaitForAppStart();
  UART_Send("audioUploadTask start\r\n");
  bool streaming = false;
  uint8_t frame_buf[AUDIO_UDP_FRAME_BYTES];
  size_t frame_len = 0U;
  size_t sent_bytes = 0U;
  size_t drop_bytes = 0U;
  bool sent_once = false;

  for (;;)
  {
    if (!App_Wifi_IsConnected())
    {
      if (s_audio_udp_ready)
      {
        AudioUpload_UdpClose();
      }
      streaming = false;
      s_audio_udp_sending = false;
      frame_len = 0U;
      osDelay(AUDIO_UPLOAD_RETRY_DELAY_MS);
      continue;
    }

    bool recording = Audio_IsRecording();
    bool stream_has_data = (frame_len > 0U) || !Audio_StreamIsEmpty();
    bool should_stream = recording || stream_has_data;

    if (!should_stream)
    {
      if (streaming)
      {
        if (s_audio_udp_ready)
        {
          AudioUpload_UdpClose();
        }
        drop_bytes = Audio_StreamGetDroppedBytes();
        if (sent_bytes > 0U || drop_bytes > 0U)
        {
          char msg[96];
          snprintf(msg, sizeof(msg),
                   "audio udp: stream stop sent %lu drop %lu\r\n",
                   (unsigned long)sent_bytes,
                   (unsigned long)drop_bytes);
          UART_Send(msg);
        }
      }
      streaming = false;
      s_audio_udp_sending = false;
      frame_len = 0U;
      sent_bytes = 0U;
      drop_bytes = 0U;
      sent_once = false;
      osDelay(AUDIO_UDP_IDLE_DELAY_MS);
      continue;
    }

    if (!s_audio_udp_ready && !AudioUpload_UdpOpen())
    {
      char msg[64];
      snprintf(msg, sizeof(msg), "audio udp: open failed (%d)\r\n",
               s_audio_upload_last_errno);
      UART_Send(msg);
      osDelay(AUDIO_UPLOAD_RETRY_DELAY_MS);
      continue;
    }

    if (!streaming)
    {
      UART_Send("audio udp: stream start\r\n");
      sent_bytes = 0U;
      drop_bytes = 0U;
      sent_once = false;
      streaming = true;
    }
    s_audio_udp_sending = true;

    size_t got = 0U;
    if (frame_len < AUDIO_UDP_FRAME_BYTES)
    {
      got = Audio_StreamRead(&frame_buf[frame_len],
                             AUDIO_UDP_FRAME_BYTES - frame_len,
                             AUDIO_UDP_READ_TIMEOUT_MS);
      if (got > 0U)
      {
        frame_len += got;
      }
    }

    if (frame_len == 0U)
    {
      osDelay(AUDIO_UDP_IDLE_DELAY_MS);
      continue;
    }

    if (frame_len < AUDIO_UDP_FRAME_BYTES && !recording && Audio_StreamIsEmpty())
    {
      /* Flush tail */
    }
    else if (frame_len < AUDIO_UDP_FRAME_BYTES)
    {
      osDelay(AUDIO_UDP_IDLE_DELAY_MS);
      continue;
    }

    if (!AudioUpload_Lock(AUDIO_UDP_LOCK_TIMEOUT_MS))
    {
      osDelay(AUDIO_UDP_IDLE_DELAY_MS);
      continue;
    }

    if (!AudioUpload_UdpSend(frame_buf, frame_len))
    {
      int err = s_audio_upload_last_errno;
      AudioUpload_Unlock();
      if (err == ENOBUFS || err == EWOULDBLOCK || err == EAGAIN)
      {
        osDelay(AUDIO_UDP_SEND_PAUSE_MS);
        continue;
      }

      char msg[64];
      snprintf(msg, sizeof(msg), "audio udp: send failed (%d)\r\n", err);
      UART_Send(msg);
      AudioUpload_UdpClose();
      osDelay(AUDIO_UPLOAD_RETRY_DELAY_MS);
      continue;
    }

    AudioUpload_Unlock();
    if (!sent_once)
    {
      UART_Send("audio udp: first packet\r\n");
      sent_once = true;
    }
    sent_bytes += frame_len;
    frame_len = 0U;
    osDelay(AUDIO_UDP_SEND_PAUSE_MS);
  }
  /* USER CODE END StartTask07 */
}

/* USER CODE BEGIN Header_StartTask06 */
/**
* @brief Function implementing the wifiInitTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask06 */
void StartTask06(void *argument)
{
  /* USER CODE BEGIN StartTask06 */
  (void)argument;
  UART_Send("wifiInitTask start\r\n");
  EnsureInterruptsEnabled();
  EnsureRtosTickRunning();
#if !WIFI_ONLY_MODE && ENABLE_UI_DRAW
  UART_Send("ui clear...\r\n");
  UI_DrawBackground();
  UART_Send("ui clear ok\r\n");
  UI_DrawStatus(g_wm_ready, g_wm_ok, g_wm_addr);
#endif

  UART_Send("wifi connect...\r\n");
  bool wifi_ok = App_Wifi_Connect("304-2.4G", "wuyidaxue123");
  if (!wifi_ok)
  {
    UART_Send("WiFi connect failed\r\n");
  }
  else
  {
    UART_Send("WiFi connected\r\n");
  }

  EnsureInterruptsEnabled();
  EnsureRtosTickRunning();

#if START_TASKS_AFTER_WIFI
  if (wifi_ok)
  {
    UART_Send("start app tasks...\r\n");
    CreateAppTasks();
  }
  else
  {
    UART_Send("WiFi failed, start app tasks anyway\r\n");
    CreateAppTasks();
  }
#endif

  UART_Send("app start signal...\r\n");
  SignalAppStart();
  osThreadExit();
  /* USER CODE END StartTask06 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static bool AudioUpload_UdpOpen(void)
{
  if (s_audio_udp_ready)
  {
    return true;
  }

  int sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0)
  {
    s_audio_upload_last_errno = errno;
    return false;
  }

  memset(&s_audio_udp_addr, 0, sizeof(s_audio_udp_addr));
#if LWIP_HAVE_SOCKADDR_LEN
  s_audio_udp_addr.sin_len = sizeof(s_audio_udp_addr);
#endif
  s_audio_udp_addr.sin_family = AF_INET;
  s_audio_udp_addr.sin_port = htons((uint16_t)AUDIO_UPLOAD_SERVER_PORT);
  if (inet_aton(AUDIO_UPLOAD_SERVER_IP, &s_audio_udp_addr.sin_addr) == 0)
  {
    s_audio_upload_last_errno = EINVAL;
    lwip_close(sock);
    return false;
  }

  s_audio_udp_socket = sock;
  s_audio_udp_ready = true;
  s_audio_upload_last_errno = 0;
  return true;
}

static void AudioUpload_UdpClose(void)
{
  if (s_audio_udp_socket >= 0)
  {
    lwip_close(s_audio_udp_socket);
    s_audio_udp_socket = -1;
  }
  s_audio_udp_ready = false;
}

static bool AudioUpload_UdpSend(const uint8_t *data, size_t len)
{
  if (!s_audio_udp_ready || data == NULL || len == 0U)
  {
    return false;
  }

  int sent = lwip_sendto(s_audio_udp_socket,
                         data,
                         (int)len,
                         0,
                         (struct sockaddr *)&s_audio_udp_addr,
                         sizeof(s_audio_udp_addr));
  if (sent < 0)
  {
    int err = errno;
    if (err == 0)
    {
      err = ENOBUFS;
    }
    s_audio_upload_last_errno = err;
    return false;
  }
  if ((size_t)sent != len)
  {
    s_audio_upload_last_errno = EMSGSIZE;
    return false;
  }

  s_audio_upload_last_errno = 0;
  return true;
}

static bool AudioUpload_Lock(uint32_t timeout_ms)
{
  if (lwipMutexHandle == NULL)
  {
    return true;
  }
  return (osMutexAcquire(lwipMutexHandle, timeout_ms) == osOK);
}

static void AudioUpload_Unlock(void)
{
  if (lwipMutexHandle == NULL)
  {
    return;
  }
  (void)osMutexRelease(lwipMutexHandle);
}

static void UART_Send(const char *msg)
{
  if (msg == NULL)
  {
    return;
  }
  if (uartMutexHandle != NULL)
  {
    (void)osMutexAcquire(uartMutexHandle, osWaitForever);
  }
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)strlen(msg), 100);
  if (uartMutexHandle != NULL)
  {
    (void)osMutexRelease(uartMutexHandle);
  }
}

static bool Audio_EnsureDmaStarted(void)
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

static void Audio_MaybeStopDma(void)
{
  if (s_audio_silence_test)
  {
    return;
  }
  if (!Audio_IsRecording() && !Audio_IsPlaying() && !Audio_StreamIsActive())
  {
    if (Audio_StopTxRxDMA())
    {
      UART_Send("I2S TxRx DMA STOP\r\n");
    }
  }
}

static bool I2C_Lock(void)
{
  if (i2cMutexHandle == NULL)
  {
    return false;
  }
  return (osMutexAcquire(i2cMutexHandle, osWaitForever) == osOK);
}

static void I2C_Unlock(void)
{
  if (i2cMutexHandle == NULL)
  {
    return;
  }
  (void)osMutexRelease(i2cMutexHandle);
}

static void EnsureInterruptsEnabled(void)
{
  if (__get_PRIMASK() != 0U)
  {
    __enable_irq();
  }
  if (__get_BASEPRI() != 0U)
  {
    __set_BASEPRI(0U);
  }
}

extern void vPortSetupTimerInterrupt(void);

static void EnsureRtosTickRunning(void)
{
  uint32_t ctrl = SysTick->CTRL;
  uint32_t load = SysTick->LOAD;

  if ((ctrl & SysTick_CTRL_ENABLE_Msk) == 0U ||
      (ctrl & SysTick_CTRL_TICKINT_Msk) == 0U ||
      load == 0U)
  {
    vPortSetupTimerInterrupt();
    NVIC_SetPriority(SysTick_IRQn,
                     (configKERNEL_INTERRUPT_PRIORITY >> (8U - __NVIC_PRIO_BITS)));
    UART_Send("RTOS tick reinit\r\n");
  }
}

static void WaitForAppStart(void)
{
  if (appStartEventHandle == NULL)
  {
    return;
  }
  (void)osEventFlagsWait(appStartEventHandle,
                         APP_START_FLAG,
                         osFlagsWaitAny,
                         osWaitForever);
}

static void SignalAppStart(void)
{
  if (appStartEventHandle == NULL)
  {
    return;
  }
  (void)osEventFlagsSet(appStartEventHandle, APP_START_FLAG);
}

/* USER CODE END Application */
