#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "cmsis_os.h"
#include "platform/wwd_platform_interface.h"
#include "platform_init.h"
#include "platform_toolchain.h"
#include "RTOS/wwd_rtos_interface.h"
#include "wwd_buffer.h"
#include "network/wwd_buffer_interface.h"
#include "wwd_constants.h"
#include "wwd_management.h"
#include "wwd_wifi.h"
#include "wiced_utilities.h"

#include "lwip/err.h"
#include "lwip/icmp.h"
#include "lwip/inet.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/mem.h"
#include "lwip/api.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/ip4_addr.h"
#include "lwip/sys.h"
#include "netif/ethernet.h"
#include "wwd_network.h"

#define WIFI_COUNTRY                WICED_COUNTRY_CHINA
#define WIFI_SECURITY               WICED_SECURITY_WPA2_MIXED_PSK
#define WIFI_SCAN_RESULTS_BUFF_SIZE 40U
#define WIFI_SCAN_BSSID_LIST_SIZE   200U
#define WIFI_SCAN_DONE_CHANNEL      0xFFU
#define WIFI_JOIN_RETRIES           3U
#define WIFI_JOIN_RETRY_DELAY_MS    1000U
#define WIFI_PING_ID                0xAFAFU
#define WIFI_PING_DATA_SIZE         32U

static bool s_wifi_initialized = false;

static struct netif s_wifi_netif;
static bool s_lwip_initialized = false;
static bool s_lwip_netif_ready = false;
static bool s_lwip_ip_ready = false;
static struct dhcp s_wifi_dhcp;

static wiced_mac_t s_scan_bssid_list[WIFI_SCAN_BSSID_LIST_SIZE];
static host_semaphore_type_t s_scan_results_semaphore;
static bool s_scan_semaphore_ready = false;
static wiced_scan_result_t s_scan_results[WIFI_SCAN_RESULTS_BUFF_SIZE];
static uint16_t s_scan_write_pos = 0;
static uint16_t s_scan_read_pos = 0;
static uint16_t s_ping_seq_num = 0;

extern osMutexId_t lwipMutexHandle;

static bool Lwip_Lock(uint32_t timeout_ms)
{
  if (lwipMutexHandle == NULL)
  {
    return true;
  }
  return (osMutexAcquire(lwipMutexHandle, timeout_ms) == osOK);
}

static void Lwip_Unlock(void)
{
  if (lwipMutexHandle == NULL)
  {
    return;
  }
  (void)osMutexRelease(lwipMutexHandle);
}

struct wifi_icmp_packet
{
  struct icmp_echo_hdr hdr;
  uint8_t data[WIFI_PING_DATA_SIZE];
};

static struct wifi_icmp_packet s_ping_packet;

static void Wifi_ScanResultsHandler(wiced_scan_result_t **result_ptr, void *user_data, wiced_scan_status_t status);
static const char *Wifi_SecurityToString(wiced_security_t security);
static void Wifi_LwipInitDone(void *arg);
static err_t Wifi_ReadIpInfo(struct netif *netif);
static void Wifi_PingPrepare(struct wifi_icmp_packet *packet, uint16_t len, uint16_t seq);
static err_t Wifi_PingSend(int socket_hnd, const ip4_addr_t *addr, uint16_t *seq_out);
static err_t Wifi_PingRecv(int socket_hnd, uint16_t seq);
static bool Wifi_ResolveHost(const char *host, ip4_addr_t *addr);

typedef struct
{
  ip4_addr_t ip;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
} wifi_ip_info_t;

static wifi_ip_info_t s_wifi_ip_info;

static bool Wifi_LwipInitOnce(void)
{
  if (s_lwip_initialized)
  {
    return true;
  }

  printf("LwIP: init start\r\n");
  SemaphoreHandle_t lwip_done = xSemaphoreCreateCounting(1, 0);
  if (lwip_done == NULL)
  {
    printf("LwIP: semaphore init failed\r\n");
    return false;
  }

  tcpip_init(Wifi_LwipInitDone, &lwip_done);
  if (xSemaphoreTake(lwip_done, pdMS_TO_TICKS(10000)) != pdTRUE)
  {
    printf("LwIP: init timeout\r\n");
    vSemaphoreDelete(lwip_done);
    return false;
  }
  vSemaphoreDelete(lwip_done);

  s_lwip_initialized = true;
  printf("LwIP: init done\r\n");
  return true;
}

static bool Wifi_LwipBringUp(void)
{
  ip4_addr_t ipaddr;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
  err_t err;
  int retries = 1000;

  if (s_lwip_ip_ready)
  {
    return true;
  }

  if (!Wifi_LwipInitOnce())
  {
    return false;
  }

  if (!s_lwip_netif_ready)
  {
    memset(&s_wifi_netif, 0, sizeof(s_wifi_netif));
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);
    ip4_addr_set_zero(&gateway);

    if (netif_add(&s_wifi_netif,
                  &ipaddr,
                  &netmask,
                  &gateway,
                  (void *)WWD_STA_INTERFACE,
                  ethernetif_init,
                  ethernet_input) == NULL)
    {
      printf("LwIP: netif add failed\r\n");
      return false;
    }

    netif_set_default(&s_wifi_netif);
    netif_set_up(&s_wifi_netif);
    dhcp_set_struct(&s_wifi_netif, &s_wifi_dhcp);
    s_lwip_netif_ready = true;
  }

  err = dhcp_start(&s_wifi_netif);
  if (err != ERR_OK)
  {
    printf("LwIP: dhcp start failed (%d)\r\n", (int)err);
    return false;
  }

  printf("LwIP: waiting for DHCP...\r\n");
  while (retries-- > 0 && !dhcp_supplied_address(&s_wifi_netif))
  {
    sys_msleep(10);
  }

  if (!dhcp_supplied_address(&s_wifi_netif))
  {
    printf("LwIP: DHCP timeout\r\n");
    return false;
  }

  Wifi_ReadIpInfo(&s_wifi_netif);
  char ip_text[16];
  char gw_text[16];
  char nm_text[16];

  ip4addr_ntoa_r(&s_wifi_ip_info.ip, ip_text, sizeof(ip_text));
  ip4addr_ntoa_r(&s_wifi_ip_info.gateway, gw_text, sizeof(gw_text));
  ip4addr_ntoa_r(&s_wifi_ip_info.netmask, nm_text, sizeof(nm_text));
  printf("LwIP: IP %s\r\n", ip_text);
  printf("LwIP: GW %s\r\n", gw_text);
  printf("LwIP: NM %s\r\n", nm_text);

  s_lwip_ip_ready = true;
  return true;
}

bool App_Wifi_IsConnected(void)
{
  if (!s_wifi_initialized || !s_lwip_ip_ready)
  {
    return false;
  }
  return (wwd_wifi_is_ready_to_transceive(WWD_STA_INTERFACE) == WWD_SUCCESS);
}

bool App_Wifi_PingHost(const char *host, uint32_t timeout_ms, uint32_t *elapsed_ms)
{
  ip4_addr_t target;
  int socket_hnd;
  int recv_timeout;
  err_t result;
  uint16_t seq = 0;
  wwd_time_t start;

  if (host == NULL || host[0] == '\0')
  {
    return false;
  }

  if (!App_Wifi_IsConnected())
  {
    return false;
  }

  if (!Lwip_Lock(0U))
  {
    return false;
  }

  if (!Wifi_ResolveHost(host, &target))
  {
    Lwip_Unlock();
    return false;
  }

  socket_hnd = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
  if (socket_hnd < 0)
  {
    Lwip_Unlock();
    return false;
  }

  recv_timeout = (int)timeout_ms;
  lwip_setsockopt(socket_hnd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

  result = Wifi_PingSend(socket_hnd, &target, &seq);
  if (result != ERR_OK)
  {
    lwip_close(socket_hnd);
    Lwip_Unlock();
    return false;
  }

  start = host_rtos_get_time();
  result = Wifi_PingRecv(socket_hnd, seq);
  if (elapsed_ms != NULL)
  {
    *elapsed_ms = (uint32_t)(host_rtos_get_time() - start);
  }

  lwip_close(socket_hnd);
  Lwip_Unlock();
  return (result == ERR_OK);
}

static void Wifi_LwipInitDone(void *arg)
{
  SemaphoreHandle_t *semaphore = (SemaphoreHandle_t *)arg;
  if (semaphore == NULL || *semaphore == NULL)
  {
    return;
  }

  (void)xSemaphoreGive(*semaphore);
}

static err_t Wifi_ReadIpInfo(struct netif *netif)
{
  ip4_addr_copy(s_wifi_ip_info.ip, *netif_ip4_addr(netif));
  ip4_addr_copy(s_wifi_ip_info.netmask, *netif_ip4_netmask(netif));
  ip4_addr_copy(s_wifi_ip_info.gateway, *netif_ip4_gw(netif));
  return ERR_OK;
}

static void Wifi_PingPrepare(struct wifi_icmp_packet *packet, uint16_t len, uint16_t seq)
{
  uint16_t i;

  ICMPH_TYPE_SET(&packet->hdr, ICMP_ECHO);
  ICMPH_CODE_SET(&packet->hdr, 0);
  packet->hdr.chksum = 0;
  packet->hdr.id = (uint16_t)WIFI_PING_ID;
  packet->hdr.seqno = htons(seq);

  for (i = 0; i < WIFI_PING_DATA_SIZE; i++)
  {
    packet->data[i] = (uint8_t)i;
  }

  packet->hdr.chksum = inet_chksum(packet, len);
}

static err_t Wifi_PingSend(int socket_hnd, const ip4_addr_t *addr, uint16_t *seq_out)
{
  struct sockaddr_in to;
  uint16_t seq;

  if (addr == NULL || seq_out == NULL)
  {
    return ERR_VAL;
  }

  seq = (uint16_t)(s_ping_seq_num + 1U);
  if (seq == 0U)
  {
    seq = 1U;
  }
  s_ping_seq_num = seq;

  Wifi_PingPrepare(&s_ping_packet, (uint16_t)sizeof(s_ping_packet), seq);

  memset(&to, 0, sizeof(to));
#if LWIP_HAVE_SOCKADDR_LEN
  to.sin_len = sizeof(to);
#endif
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = addr->addr;

  if (lwip_sendto(socket_hnd,
                  &s_ping_packet,
                  sizeof(s_ping_packet),
                  0,
                  (struct sockaddr *)&to,
                  sizeof(to)) <= 0)
  {
    return ERR_VAL;
  }

  *seq_out = seq;
  return ERR_OK;
}

static err_t Wifi_PingRecv(int socket_hnd, uint16_t seq)
{
  char buf[64];
  struct sockaddr_in from;
  socklen_t fromlen = sizeof(from);
  int len;
  struct ip_hdr *iphdr;
  struct icmp_echo_hdr *iecho;

  do
  {
    len = lwip_recvfrom(socket_hnd,
                        buf,
                        sizeof(buf),
                        0,
                        (struct sockaddr *)&from,
                        &fromlen);

    if (len >= (int)(sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr)))
    {
      iphdr = (struct ip_hdr *)buf;
      iecho = (struct icmp_echo_hdr *)(buf + (IPH_HL(iphdr) * 4));
      if ((iecho->id == (uint16_t)WIFI_PING_ID) &&
          (iecho->seqno == htons(seq)) &&
          (ICMPH_TYPE(iecho) == ICMP_ER))
      {
        return ERR_OK;
      }
    }
  } while (len > 0);

  return ERR_TIMEOUT;
}

static bool Wifi_ResolveHost(const char *host, ip4_addr_t *addr)
{
  ip_addr_t resolved;
  err_t err;

  if (host == NULL || addr == NULL)
  {
    return false;
  }

  if (ip4addr_aton(host, addr))
  {
    return true;
  }

  err = netconn_gethostbyname(host, &resolved);
  if (err != ERR_OK)
  {
    return false;
  }

  if (!IP_IS_V4(&resolved))
  {
    return false;
  }

  ip4_addr_copy(*addr, *ip_2_ip4(&resolved));
  return true;
}

static bool Wifi_InitOnce(void)
{
  wwd_result_t result;

  if (s_wifi_initialized)
  {
    return true;
  }

  /* LwIP init must run before wifi_on to ensure pbuf pools are ready. */
  if (!Wifi_LwipInitOnce())
  {
    printf("WiFi: LwIP init failed\r\n");
    return false;
  }

  printf("WiFi: buffer init...\r\n");
  result = wwd_buffer_init(NULL);
  if (result != WWD_SUCCESS)
  {
    printf("WiFi: buffer init failed (%d)\r\n", (int)result);
    return false;
  }

  printf("WiFi: wifi on...\r\n");
  result = wwd_management_wifi_on(WIFI_COUNTRY);
  if (result != WWD_SUCCESS)
  {
    printf("WiFi: wwd_management_wifi_on failed (%d)\r\n", (int)result);
    return false;
  }

  s_wifi_initialized = true;
  printf("WiFi: init done\r\n");
  return true;
}

static bool Wifi_ScanSemaphoreInit(void)
{
  if (s_scan_semaphore_ready)
  {
    return true;
  }

  if (host_rtos_init_semaphore(&s_scan_results_semaphore) != WWD_SUCCESS)
  {
    printf("WiFi: scan semaphore init failed\r\n");
    return false;
  }

  s_scan_semaphore_ready = true;
  return true;
}

bool App_Wifi_Connect(const char *ssid, const char *password)
{
  wwd_result_t result;
  wiced_ssid_t ap_ssid;
  size_t ssid_len;
  size_t pass_len;
  uint32_t attempt = 0U;

  if (ssid == NULL || ssid[0] == '\0')
  {
    return false;
  }

  printf("WiFi: connect start\r\n");
  if (!Wifi_InitOnce())
  {
    return false;
  }

  ssid_len = strlen(ssid);
  if (ssid_len > sizeof(ap_ssid.value))
  {
    printf("WiFi: SSID too long\r\n");
    return false;
  }

  memset(&ap_ssid, 0, sizeof(ap_ssid));
  ap_ssid.length = (uint8_t)ssid_len;
  memcpy(ap_ssid.value, ssid, ssid_len);

  if (password == NULL)
  {
    password = "";
  }
  pass_len = strlen(password);

  printf("WiFi: joining \"%s\"...\r\n", ssid);
  for (attempt = 1U; attempt <= WIFI_JOIN_RETRIES; attempt++)
  {
    result = wwd_wifi_join(&ap_ssid, WIFI_SECURITY, (const uint8_t *)password,
                           (uint8_t)pass_len, NULL, WWD_STA_INTERFACE);
    if (result == WWD_SUCCESS)
    {
      break;
    }
    printf("WiFi: join attempt %lu failed (%d)\r\n",
           (unsigned long)attempt, (int)result);
    if (attempt < WIFI_JOIN_RETRIES)
    {
      host_rtos_delay_milliseconds(WIFI_JOIN_RETRY_DELAY_MS);
    }
  }
  if (result != WWD_SUCCESS)
  {
    printf("WiFi: join failed (%d)\r\n", (int)result);
    return false;
  }

  printf("WiFi: joined SSID \"%s\"\r\n", ssid);
  printf("WiFi: bring up LwIP\r\n");
  if (!Wifi_LwipBringUp())
  {
    printf("WiFi: DHCP failed\r\n");
    return false;
  }

  return true;
}

bool App_Wifi_ScanOnce(void)
{
  wiced_scan_result_t *result_ptr = &s_scan_results[0];
  wwd_result_t result;
  int record_number = 1;

  if (!Wifi_InitOnce())
  {
    return false;
  }

  if (!Wifi_ScanSemaphoreInit())
  {
    return false;
  }

  memset(s_scan_bssid_list, 0, sizeof(s_scan_bssid_list));
  s_scan_read_pos = 0;
  s_scan_write_pos = 0;

  printf("WiFi: scan start\r\n");
  result = wwd_wifi_scan(WICED_SCAN_TYPE_ACTIVE,
                         WICED_BSS_TYPE_ANY,
                         NULL,
                         NULL,
                         NULL,
                         NULL,
                         Wifi_ScanResultsHandler,
                         &result_ptr,
                         NULL,
                         WWD_STA_INTERFACE);
  if (result != WWD_SUCCESS)
  {
    printf("WiFi: scan start failed (%d)\r\n", (int)result);
    return false;
  }

  printf("WiFi: waiting for scan results...\r\n");
  while (host_rtos_get_semaphore(&s_scan_results_semaphore, NEVER_TIMEOUT, WICED_FALSE) == WWD_SUCCESS)
  {
    wiced_scan_result_t *record = &s_scan_results[s_scan_read_pos];

    if (record->channel == WIFI_SCAN_DONE_CHANNEL)
    {
      break;
    }

    printf("\r\n#%03d SSID          : %.*s\r\n",
           record_number,
           (int)record->SSID.length,
           (const char *)record->SSID.value);
    printf("     BSSID         : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           (unsigned int)record->BSSID.octet[0],
           (unsigned int)record->BSSID.octet[1],
           (unsigned int)record->BSSID.octet[2],
           (unsigned int)record->BSSID.octet[3],
           (unsigned int)record->BSSID.octet[4],
           (unsigned int)record->BSSID.octet[5]);
    printf("     RSSI          : %ddBm%s\r\n",
           (int)record->signal_strength,
           (record->flags & WICED_SCAN_RESULT_FLAG_RSSI_OFF_CHANNEL) ? " (off-channel)" : "");
    printf("     Max Data Rate : %.1f Mbits/s\r\n", (float)record->max_data_rate / 1000.0f);
    printf("     Network Type  : %s\r\n",
           (record->bss_type == WICED_BSS_TYPE_INFRASTRUCTURE) ? "Infrastructure" :
           (record->bss_type == WICED_BSS_TYPE_ADHOC) ? "Ad hoc" : "Unknown");
    printf("     Security      : %s\r\n", Wifi_SecurityToString(record->security));
    printf("     Radio Band    : %s\r\n", (record->band == WICED_802_11_BAND_5GHZ) ? "5GHz" : "2.4GHz");
    printf("     Channel       : %d\r\n", (int)record->channel);

    s_scan_read_pos++;
    if (s_scan_read_pos >= WIFI_SCAN_RESULTS_BUFF_SIZE)
    {
      s_scan_read_pos = 0;
    }
    record_number++;
  }

  printf("\r\nWiFi: scan done\r\n");
  return true;
}

static void Wifi_ScanResultsHandler(wiced_scan_result_t **result_ptr, void *user_data, wiced_scan_status_t status)
{
  (void)user_data;
  (void)status;

  if (result_ptr == NULL)
  {
    s_scan_results[s_scan_write_pos].channel = WIFI_SCAN_DONE_CHANNEL;
    host_rtos_set_semaphore(&s_scan_results_semaphore, WICED_FALSE);
    return;
  }

  wiced_scan_result_t *record = *result_ptr;
  wiced_mac_t *tmp_mac = s_scan_bssid_list;
  size_t i = 0;

  for (i = 0; i < WIFI_SCAN_BSSID_LIST_SIZE; i++)
  {
    if (NULL_MAC(tmp_mac->octet) == WICED_TRUE)
    {
      break;
    }
    if (CMP_MAC(tmp_mac->octet, record->BSSID.octet) == WICED_TRUE)
    {
      return;
    }
    tmp_mac++;
  }

  if (i >= WIFI_SCAN_BSSID_LIST_SIZE)
  {
    return;
  }

  memcpy(tmp_mac->octet, record->BSSID.octet, sizeof(wiced_mac_t));

  s_scan_write_pos++;
  if (s_scan_write_pos >= WIFI_SCAN_RESULTS_BUFF_SIZE)
  {
    s_scan_write_pos = 0;
  }

  *result_ptr = &s_scan_results[s_scan_write_pos];
  host_rtos_set_semaphore(&s_scan_results_semaphore, WICED_FALSE);
}

static const char *Wifi_SecurityToString(wiced_security_t security)
{
  switch (security)
  {
    case WICED_SECURITY_OPEN:
      return "Open";
    case WICED_SECURITY_WEP_PSK:
      return "WEP";
    case WICED_SECURITY_WPA_TKIP_PSK:
      return "WPA";
    case WICED_SECURITY_WPA2_AES_PSK:
      return "WPA2 AES";
    case WICED_SECURITY_WPA2_MIXED_PSK:
      return "WPA2 Mixed";
    default:
      return "Unknown";
  }
}
