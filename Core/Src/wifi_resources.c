#include <string.h>

#include "platform/wwd_resource_interface.h"
#include "wiced_resource.h"
#include "wifi_nvram_image.h"

extern const resource_hnd_t wifi_firmware_image;

static wwd_result_t wifi_get_resource_data(wwd_resource_t resource, const uint8_t **data_out, uint32_t *size_out)
{
  if (data_out == NULL || size_out == NULL)
  {
    return WWD_BADARG;
  }

  if (resource == WWD_RESOURCE_WLAN_FIRMWARE)
  {
    *data_out = (const uint8_t *)wifi_firmware_image.val.mem.data;
    *size_out = (uint32_t)wifi_firmware_image.size;
    return WWD_SUCCESS;
  }

  if (resource == WWD_RESOURCE_WLAN_NVRAM)
  {
    *data_out = (const uint8_t *)wifi_nvram_image;
    *size_out = (uint32_t)sizeof(wifi_nvram_image);
    return WWD_SUCCESS;
  }

  return WWD_UNSUPPORTED;
}

wwd_result_t host_platform_resource_size(wwd_resource_t resource, uint32_t *size_out)
{
  const uint8_t *data = NULL;
  uint32_t size = 0;
  wwd_result_t result = wifi_get_resource_data(resource, &data, &size);

  if (result != WWD_SUCCESS)
  {
    if (size_out != NULL)
    {
      *size_out = 0;
    }
    return result;
  }

  *size_out = size;
  return WWD_SUCCESS;
}

wwd_result_t host_platform_resource_read_direct(wwd_resource_t resource, const void **ptr_out)
{
  const uint8_t *data = NULL;
  uint32_t size = 0;
  wwd_result_t result = wifi_get_resource_data(resource, &data, &size);

  if (result != WWD_SUCCESS)
  {
    if (ptr_out != NULL)
    {
      *ptr_out = NULL;
    }
    return result;
  }

  *ptr_out = data;
  return WWD_SUCCESS;
}

wwd_result_t host_platform_resource_read_indirect(wwd_resource_t resource, uint32_t offset, void *buffer,
                                                  uint32_t buffer_size, uint32_t *size_out)
{
  const uint8_t *data = NULL;
  uint32_t size = 0;
  wwd_result_t result = wifi_get_resource_data(resource, &data, &size);

  if (result != WWD_SUCCESS)
  {
    if (size_out != NULL)
    {
      *size_out = 0;
    }
    return result;
  }

  if (offset >= size)
  {
    *size_out = 0;
    return WWD_SUCCESS;
  }

  size -= offset;
  if (size > buffer_size)
  {
    size = buffer_size;
  }

  memcpy(buffer, data + offset, size);
  *size_out = size;
  return WWD_SUCCESS;
}
