#include "app_input.h"

#include "main.h"

#define KEY_DEBOUNCE_MS 50U

static volatile uint8_t s_key1_events = 0;
static volatile uint8_t s_key2_events = 0;
static uint32_t s_key1_tick = 0;
static uint32_t s_key2_tick = 0;

static void Input_RaiseEvent(volatile uint8_t *counter)
{
  if (*counter < 0xFF)
  {
    (*counter)++;
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint32_t now = HAL_GetTick();

  if (GPIO_Pin == KEY1_Pin)
  {
    if ((now - s_key1_tick) >= KEY_DEBOUNCE_MS)
    {
      if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET)
      {
        Input_RaiseEvent(&s_key1_events);
        s_key1_tick = now;
      }
    }
    return;
  }

  if (GPIO_Pin == KEY2_Pin)
  {
    if ((now - s_key2_tick) >= KEY_DEBOUNCE_MS)
    {
      if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET)
      {
        Input_RaiseEvent(&s_key2_events);
        s_key2_tick = now;
      }
    }
  }
}

static bool Input_ConsumeEvent(volatile uint8_t *counter)
{
  bool pressed = false;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  if (*counter > 0U)
  {
    (*counter)--;
    pressed = true;
  }

  if (primask == 0U)
  {
    __enable_irq();
  }

  return pressed;
}

bool Input_Key1Pressed(void)
{
  return Input_ConsumeEvent(&s_key1_events);
}

bool Input_Key2Pressed(void)
{
  return Input_ConsumeEvent(&s_key2_events);
}
