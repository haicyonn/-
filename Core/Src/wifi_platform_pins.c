#include "platform_cmsis.h"
#include "platform_config.h"
#include "platform_init.h"
#include "platform_peripheral.h"
#include "wwd_platform_common.h"

/* AP6181 SDIO pin map from the wifi_lwip_scan reference project. */
const platform_gpio_t wifi_control_pins[] =
{
  { 0, 0 },          /* WWD_PIN_POWER */
  { GPIOB, 13 },     /* WWD_PIN_RESET */
  { 0, 0 },          /* WWD_PIN_32K_CLK */
  { 0, 0 },          /* WWD_PIN_BOOTSTRAP_0 */
  { 0, 0 },          /* WWD_PIN_BOOTSTRAP_1 */
};

const platform_gpio_t wifi_sdio_pins[] =
{
  { 0, 0 },          /* WWD_PIN_SDIO_OOB_IRQ (PA0 is KEY1, keep OOB disabled) */
  { GPIOC, 12 },     /* WWD_PIN_SDIO_CLK */
  { GPIOD, 2 },      /* WWD_PIN_SDIO_CMD */
  { GPIOC, 8 },      /* WWD_PIN_SDIO_D0 */
  { GPIOC, 9 },      /* WWD_PIN_SDIO_D1 */
  { GPIOC, 10 },     /* WWD_PIN_SDIO_D2 */
  { GPIOC, 11 },     /* WWD_PIN_SDIO_D3 */
};

const platform_gpio_t wifi_spi_pins[] =
{
  { 0, 0 },          /* WWD_PIN_SPI_IRQ */
  { 0, 0 },          /* WWD_PIN_SPI_CS */
  { 0, 0 },          /* WWD_PIN_SPI_CLK */
  { 0, 0 },          /* WWD_PIN_SPI_MOSI */
  { 0, 0 },          /* WWD_PIN_SPI_MISO */
};

void platform_init_peripheral_irq_priorities(void)
{
  NVIC_SetPriority(SDIO_IRQn, 12);
  NVIC_SetPriority(DMA2_Stream3_IRQn, 13);
}
