#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>

#define HTTP_IMPLEMENTATION
#define HTTP_IMPLEMENTATION_LOG_IP false
#include "http.h"

#define SYS_IMPLEMENTATION
#include "sys.h"

#define PORT 8888

void* handle_client(void* arg);
void handle_routing(int client_soc, HttpRequest* req);

#define get_respond_with_mach_error(client_soc, req, path, function, err_code) do {                                                                     \
  fprintf(stderr, "%s errored with %i inside %s. Returning an error.\n", path, err_code, function);                                                     \
  http_respond(client_soc, req, "500 Internal Server Error", "application/json", "{\"error\": \"sys error\"}", strlen("{\"error\": \"sys error\"}"));   \
} while(0);

void get_api_cpu(int client_soc, HttpRequest* req);
void get_api_memory(int client_soc, HttpRequest* req);
void get_api_network(int client_soc, HttpRequest* req);
void get_api_disk(int client_soc, HttpRequest* req);
void get_api_battery(int client_soc, HttpRequest* req);
void get_api_uptime(int client_soc, HttpRequest* req);

int main(void) {
  int server_soc = http_soc_setup();
  if (server_soc < 0) exit(EXIT_FAILURE);
  if (http_soc_listen(server_soc, PORT) < 0) exit(EXIT_FAILURE);
  printf("Server started at http://localhost:%d\n", PORT);

  while (1) {
    int client_soc = http_client_accept(server_soc);
    if (client_soc < 0) {
      perror("client_soc accept failed");
      continue;
    }

    int* thread_client_soc = malloc(sizeof(int));
    *thread_client_soc = client_soc;

    pthread_t thread;
    pthread_create(&thread, NULL, handle_client, thread_client_soc);
    pthread_detach(thread);
  }

  http_soc_close(server_soc);
  return 0;
}

void* handle_client(void* args) {
  int client_soc = *(int*)args;
  free(args);

  HttpRequest req = {0};
  char *http_err = NULL;
  int parse_err_code = http_parse_request(client_soc, &req);
  switch (parse_err_code) {
    case  0: break;
    case -1: perror("Server Error: Allocation issue");                      http_err = "500 Internal Server Error"; break;
    case -2: perror("Server Error: Socket read issue");                     http_err = "500 Internal Server Error"; break;
    case -3: fprintf(stderr, "Client Error: Malformed request line\n");     http_err = "400 Bad Request";           break;
    case -4: fprintf(stderr, "Client Error: Malformed HTTP header\n");      http_err = "400 Bad Request";           break;
    case -5: fprintf(stderr, "Client Error: Request body too large\n");     http_err = "413 Payload Too Large";     break;
    case -6: fprintf(stderr, "Client Error: Malformed request body\n");     http_err = "400 Bad Request";           break;
    default: fprintf(stderr, "Server Error: Unknown %d\n", parse_err_code); http_err = "500 Internal Server Error"; break;
  }
  if (http_err) {
    char* response = "An error occurred while parsing the request.";
    http_respond(client_soc, &req, http_err, "text/plain", response, strlen(response));
    return NULL;
  }

  handle_routing(client_soc, &req);
  return NULL;
}

void handle_routing(int client_soc, HttpRequest* req) {
  // assets
  if (http_route_get(req, "/favicon") || http_route_get(req, "/favicon.ico")) { http_respond_with_file(client_soc, req, "./public/favicon.ico", "image/ico"); return; }

  // pages
  if (http_route_get(req, "/") || http_route_get(req, "/index") || http_route_get(req, "/index.html")) { http_respond_with_text_file(client_soc, req, "./public/index.html", "text/html"); return; }
  if (http_route_get(req, "/404") || http_route_get(req, "/404.html")) { http_respond_with_text_file(client_soc, req, "./public/404.html", "text/html"); return; }

  // api
  if (http_route_get(req, "/api/health")) {
    char* response = "{\"status\": \"healthy\"}";
    http_respond(client_soc, req, "200 OK", "application/json", response, strlen(response));
    return;
  }
  if (http_route_get(req, "/api/cpu")) { get_api_cpu(client_soc, req); return; }
  if (http_route_get(req, "/api/memory")) { get_api_memory(client_soc, req); return; }
  if (http_route_get(req, "/api/network")) { get_api_network(client_soc, req); return; }
  if (http_route_get(req, "/api/disk")) { get_api_disk(client_soc, req); return; }
  if (http_route_get(req, "/api/uptime")) { get_api_uptime(client_soc, req); return; }
  if (http_route_get(req, "/api/battery")) { get_api_battery(client_soc, req); return; }

  // fallback 404
  http_respond_with_text_file(client_soc, req, "./public/404.html", "text/html");
}

void get_api_cpu(int client_soc, HttpRequest* req) {
  float user_usage_percentage, system_usage_percentage, idle_usage_percentage, nice_usage_percentage, total_usage_percentage;
  int err_code = sys_get_cpu_stats(&user_usage_percentage, &system_usage_percentage, &idle_usage_percentage, &nice_usage_percentage, &total_usage_percentage);
  if (err_code < 0) {
    get_respond_with_mach_error(client_soc, req, "/api/cpu", "sys_get_cpu_stats", err_code);
    return;
  }

  char response[256];
  snprintf(response, sizeof(response), "{\"user\": %.1f, \"system\": %.1f, \"idle\": %.1f, \"nice\": %.1f, \"total\": %.1f}",
            user_usage_percentage, system_usage_percentage, idle_usage_percentage, nice_usage_percentage, total_usage_percentage);
  http_respond(client_soc, req, "200 OK", "application/json", response, strlen(response));
}

void get_api_memory(int client_soc, HttpRequest* req) {
  uint64_t total_mem, used_mem, app_mem, wired_mem, compressed_mem, cached_files, swap_used;
  int err_code = sys_get_mem_stats(&total_mem, &used_mem, &app_mem, &wired_mem, &compressed_mem, &cached_files, &swap_used);
  if (err_code < 0) {
    get_respond_with_mach_error(client_soc, req, "/api/memory", "sys_get_mem_stats", err_code);
    return;
  }

  char response[256];
  snprintf(response, sizeof(response),
      "{\"total\": %" PRIu64 ", \"used\": %" PRIu64 ", \"app\": %" PRIu64 ", \"wired\": %" PRIu64 ", \"compressed\": %" PRIu64 ", \"cached\": %" PRIu64 ", \"swap_used\": %" PRIu64 "}",
      total_mem, used_mem, app_mem, wired_mem, compressed_mem, cached_files, swap_used);
  http_respond(client_soc, req, "200 OK", "application/json", response, strlen(response));
}

void get_api_network(int client_soc, HttpRequest* req) {
  double rx_bps, tx_bps;
  int err_code = sys_get_network_stats(&rx_bps, &tx_bps);
  if (err_code < 0) {
    get_respond_with_mach_error(client_soc, req, "/api/network", "sys_get_network_stats", err_code);
    return;
  }

  char response[256];
  snprintf(response, sizeof(response), "{\"rx_bps\": %.0f, \"tx_bps\": %.0f}", rx_bps, tx_bps);
  http_respond(client_soc, req, "200 OK", "application/json", response, strlen(response));
}

void get_api_disk(int client_soc, HttpRequest* req) {
  uint64_t total_bytes, free_bytes;
  float percentage_used;
  int err_code = sys_get_disk_stats(&total_bytes, &free_bytes, &percentage_used);
  if (err_code < 0) {
    get_respond_with_mach_error(client_soc, req, "/api/disk", "sys_get_disk_stats", err_code);
    return;
  }

  char response[256];
  snprintf(response, sizeof(response), "{\"total_bytes\": %" PRIu64 ", \"free_bytes\": %" PRIu64 ", \"percentage_used\": %.1f}", total_bytes, free_bytes, percentage_used);
  http_respond(client_soc, req, "200 OK", "application/json", response, strlen(response));
}

void get_api_battery(int client_soc, HttpRequest* req) {
  int percent;
  bool is_charging;
  int err_code = sys_get_battery_stats(&percent, &is_charging);
  if (err_code < 0) {
    get_respond_with_mach_error(client_soc, req, "/api/battery", "sys_get_battery_stats", err_code);
    return;
  }

  char response[256];
  snprintf(response, sizeof(response), "{\"percent\": %d, \"is_charging\": %s}", percent, is_charging ? "true" : "false");
  http_respond(client_soc, req, "200 OK", "application/json", response, strlen(response));
}

void get_api_uptime(int client_soc, HttpRequest* req) {
  long uptime;
  int err_code = sys_get_uptime(&uptime);
  if (err_code < 0) {
    get_respond_with_mach_error(client_soc, req, "/api/uptime", "sys_get_uptime", err_code);
    return;
  }

  char response[256];
  snprintf(response, sizeof(response), "{\"uptime_sec\": %ld}", uptime);
  http_respond(client_soc, req, "200 OK", "application/json", response, strlen(response));
}
