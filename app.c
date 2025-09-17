/******************************************************************************
 * app_http_server_si70xx.c
 *
 * Smart Precision Agriculture Dashboard
 * Fetches 3-day weather forecast from backend and displays it on a web server.
 ******************************************************************************/
#include <stdint.h>
#ifndef u_int32_t
typedef uint32_t u_int32_t;
#endif

#include "sl_board_configuration.h"
#include "cmsis_os2.h"
#include "sl_wifi.h"
#include "sl_net.h"
#include "sl_net_si91x.h"
#include "sl_utility.h"
#include "socket.h"
#include "sl_si91x_core_utilities.h"
#include "errno.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "socket.h"
#include "sl_net.h"
#include "sl_wifi.h"

#include "sl_si91x_si70xx.h"

#ifndef I2C
#define I2C SL_I2C2
#endif
#ifndef SI70XX_SLAVE_ADDR
#define SI70XX_SLAVE_ADDR 0x40
#endif

#define HTTP_SERVER_PORT 80
#define BACKLOG 2
#define RECV_BUF_SIZE 1024
#define BACKEND_HOST "10.143.82.93"
#define BACKEND_PORT 3000
#define BACKEND_PATH "/api/weather/forecast"
#define BACKEND_TIMEOUT_MS 3000  // Reduced from 5000 to 3000ms

/* Print assigned IP */
static void print_ip_from_ipconfig(sl_net_ip_configuration_t *ipcfg)
{
  if (ipcfg && ipcfg->type == SL_IPV4) {
    sl_ip_address_t ip = {0};
    ip.type = SL_IPV4;
    ip.ip.v4.value = ipcfg->ip.v4.ip_address.value;
    print_sl_ip_address(&ip);
  }
}

/* ------------------ Weather fetch ------------------ */
sl_status_t fetch_weather(char *weather_str, int str_len) {
  int sock = -1;
  struct sockaddr_in server_addr = {0};
  char recv_buf[512];  // Increased buffer size
  int len;
  int ret;
  sl_status_t result = SL_STATUS_FAIL;

  // Use direct IP to avoid DNS issues
  uint32_t ip = 0;
  sl_status_t status = sl_net_inet_addr(BACKEND_HOST, &ip);
  if (status != SL_STATUS_OK) {
    printf("Invalid IP address: %s\n", BACKEND_HOST);
    return SL_STATUS_FAIL;
  }
  server_addr.sin_addr.s_addr = ip;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    printf("API socket() failed: %d\n", errno);
    return SL_STATUS_FAIL;
  }

  // Set shorter timeout
  struct timeval timeout;
  timeout.tv_sec = BACKEND_TIMEOUT_MS / 1000;
  timeout.tv_usec = (BACKEND_TIMEOUT_MS % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(BACKEND_PORT);

  printf("Connecting to backend...\n");
  ret = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret < 0) {
    printf("Backend connect() failed: %d\n", errno);
    goto cleanup;
  }

  // HTTP request
  char request[128];
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
           BACKEND_PATH, BACKEND_HOST);

  printf("Sending request...\n");
  ret = send(sock, request, strlen(request), 0);
  if (ret < 0) {
    printf("Backend send() failed: %d\n", errno);
    goto cleanup;
  }

  memset(recv_buf, 0, sizeof(recv_buf));
  len = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
  if (len <= 0) {
    printf("Backend recv() failed: %d\n", errno);
    goto cleanup;
  }
  recv_buf[len] = '\0';

  // Parse HTTP response
  char *data_start = strstr(recv_buf, "\r\n\r\n");
  if (data_start) {
    data_start += 4;
    strncpy(weather_str, data_start, str_len - 1);
    weather_str[str_len - 1] = '\0';

    // Clean up string
    char *end = weather_str + strlen(weather_str) - 1;
    while (end >= weather_str && (*end == '\n' || *end == '\r' || *end == ' ')) {
      *end = '\0';
      end--;
    }
    result = SL_STATUS_OK;
  }

cleanup:
  if (sock >= 0) {
    close(sock);
  }
  return result;
}

/* ------------------ HTTP client handler ------------------ */
static int handle_http_client(int client_sock)
{
  char recv_buf[128];
  int rc = recv(client_sock, recv_buf, sizeof(recv_buf) - 1, 0);
  if (rc <= 0) {
    close(client_sock);
    return -1;
  }
  recv_buf[rc] = '\0';

  // Check if this is a GET request
  if (!strstr(recv_buf, "GET ")) {
    close(client_sock);
    return -1;
  }

  char weather_string[384] = "🌤 Weather data unavailable";
  sl_status_t fetch_status = fetch_weather(weather_string, sizeof(weather_string));

  // Process the weather string to convert newlines to HTML breaks
  char formatted_weather[512] = {0};
  if (fetch_status == SL_STATUS_OK) {
    char *line = strtok(weather_string, "\n");
    while (line != NULL) {
      // Trim whitespace from the line
      while (*line == ' ' || *line == '\t') line++;

      // Skip empty lines
      if (strlen(line) > 0) {
        strcat(formatted_weather, line);
        strcat(formatted_weather, "<br>");
      }
      line = strtok(NULL, "\n");
    }
  } else {
    strcpy(formatted_weather, "🌤 27°C, 65% humidity, 12km/h NE");
  }

  // Send HTTP response in one go to avoid partial sends
  char full_response[1024];
  snprintf(full_response, sizeof(full_response),
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Farm Dashboard</title>"
    "</head>"
    "<body><div class='container'><h1>🌾 Smart Agriculture</h1>"
    "<div class='weather-data'>%s</div>"
    "%s"  // Error message if needed
    "<div class='footer'>3-Day Forecast | SiWx917</div></div></body></html>",
    formatted_weather,
    (fetch_status != SL_STATUS_OK) ? "<div class='error'>⚠ Could not fetch live data</div>" : "");

  int send_result = send(client_sock, full_response, strlen(full_response), 0);

  close(client_sock);
  return (send_result < 0) ? -1 : 0;
}

/* ------------------ HTTP server loop ------------------ */
static void http_server_task(void *arg)
{
  (void)arg;

  int server_sock;
  struct sockaddr_in server_addr = {0};
  int opt = 1;

  server_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sock < 0) {
    printf("HTTP server socket() failed: %d\n", errno);
    return;
  }

  setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(HTTP_SERVER_PORT);
  server_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    printf("HTTP server bind() failed: %d\n", errno);
    close(server_sock);
    return;
  }

  if (listen(server_sock, BACKLOG) < 0) {
    printf("HTTP server listen() failed: %d\n", errno);
    close(server_sock);
    return;
  }

  printf("✅ HTTP server listening on port %d\n", HTTP_SERVER_PORT);

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addrlen);

    if (client_sock < 0) {
      printf("Accept failed: %d - continuing...\n", errno);
      osDelay(100);
      continue;
    }

    printf("New client connected\n");
    handle_http_client(client_sock);
    printf("Client handled\n");

    osDelay(10); // Reduced delay
  }

  close(server_sock);
}

/* ------------------ App entry ------------------ */
void app_init(const void *unused)
{
  UNUSED_PARAMETER(unused);
  sl_status_t status;

  status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, NULL, NULL, NULL);
  if (status != SL_STATUS_OK) {
    printf("sl_net_init failed: 0x%08lx\n", status);
    return;
  }
  printf("Net init OK\n");

  status = sl_net_up(SL_NET_WIFI_CLIENT_INTERFACE, 0);
  if (status != SL_STATUS_OK) {
    printf("sl_net_up failed: 0x%08lx\n", status);
    return;
  }
  printf("Net up OK\n");

  sl_net_ip_configuration_t ip_cfg = {0};
  ip_cfg.type = SL_IPV4;
  ip_cfg.mode = SL_IP_MANAGEMENT_DHCP;
  status = sl_si91x_configure_ip_address(&ip_cfg, SL_SI91X_WIFI_CLIENT_VAP_ID);
  if (status == SL_STATUS_OK) {
    printf("IP Address: ");
    print_ip_from_ipconfig(&ip_cfg);
  } else {
    printf("IP configuration failed: 0x%08lx\n", status);
  }

  printf("Starting HTTP server...\n");

  // Create a separate task for the HTTP server instead of blocking app_init
  osThreadAttr_t thread_attr = {
    .name = "http_server",
    .stack_size = 4096,
    .priority = osPriorityNormal,
  };

  osThreadNew(http_server_task, NULL, &thread_attr);
}
