#include <string.h>

#include "platform_cmsis.h"
#include "platform_peripheral.h"
#include "wiced_utilities.h"
#include "wwd_constants.h"
#include "network/wwd_buffer_interface.h"
#include "network/wwd_network_interface.h"
#include "tlv.h"

wiced_bool_t host_platform_is_in_interrupt_context(void)
{
  return ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0U) ? WICED_TRUE : WICED_FALSE;
}

uint32_t host_platform_get_cycle_count(void)
{
  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  }

  return DWT->CYCCNT;
}

platform_result_t platform_stdio_init(platform_uart_driver_t *driver,
                                      const platform_uart_t *interface,
                                      const platform_uart_config_t *config)
{
  (void)driver;
  (void)interface;
  (void)config;
  return PLATFORM_SUCCESS;
}

void platform_stdio_write(const char *str, uint32_t len)
{
  (void)str;
  (void)len;
}

void platform_stdio_read(char *str, uint32_t len)
{
  (void)str;
  (void)len;
}

void platform_stdio_exception_write(const char *str, uint32_t len)
{
  (void)str;
  (void)len;
}

char *strnstrn(const char *s, uint16_t s_len, const char *substr, uint16_t substr_len)
{
  uint16_t i;

  if (s == NULL || substr == NULL || substr_len == 0U)
  {
    return (char *)s;
  }

  if (s_len < substr_len)
  {
    return NULL;
  }

  for (i = 0; i <= (uint16_t)(s_len - substr_len); i++)
  {
    if (memcmp(s + i, substr, substr_len) == 0)
    {
      return (char *)(s + i);
    }
  }

  return NULL;
}

char *wiced_ether_ntoa(const uint8_t *ea, char *buf, uint8_t buf_len)
{
  static const char hex[] = "0123456789abcdef";
  char *output = buf;
  const uint8_t *octet = ea;

  if (buf == NULL || ea == NULL)
  {
    return buf;
  }

  if (buf_len < WICED_ETHER_ADDR_STR_LEN)
  {
    if (buf_len > 0U)
    {
      buf[0] = '\0';
    }
    return buf;
  }

  for (; octet != &ea[WICED_ETHER_ADDR_LEN]; octet++)
  {
    *output++ = hex[(*octet >> 4) & 0x0F];
    *output++ = hex[*octet & 0x0F];
    *output++ = ':';
  }

  *(output - 1) = '\0';
  return buf;
}

tlv8_data_t *tlv_find_tlv8(const uint8_t *message, uint32_t message_length, uint8_t type)
{
  while (message_length != 0U)
  {
    uint8_t current_tlv_type = message[0];
    uint16_t current_tlv_length = (uint16_t)message[1] + 2U;

    if (current_tlv_length > message_length)
    {
      return NULL;
    }

    if (current_tlv_type == type)
    {
      return (tlv8_data_t *)message;
    }

    message += current_tlv_length;
    message_length -= current_tlv_length;
  }

  return NULL;
}
