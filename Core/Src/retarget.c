#include "usart.h"

#include <stdio.h>

#pragma import(__use_no_semihosting)

struct __FILE
{
  int handle;
};

FILE __stdout;
FILE __stdin;

void _sys_exit(int x)
{
  (void)x;
}

int _ttywrch(int ch)
{
  uint8_t c = (uint8_t)ch;
  (void)HAL_UART_Transmit(&huart1, &c, 1, 0xFFFF);
  return ch;
}

int fputc(int ch, FILE *f)
{
  uint8_t c = (uint8_t)ch;
  (void)f;
  (void)HAL_UART_Transmit(&huart1, &c, 1, 0xFFFF);
  return ch;
}
