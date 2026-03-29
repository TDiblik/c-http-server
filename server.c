#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_IMPLEMENTATION
#define HTTP_IMPLEMENTATION_LOG_IP true
#include "http.h"

#define PORT 8888

void* handle_client(void* arg);
void handle_routing(int client_soc, HttpRequest* req);

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
  if (http_err != NULL) {
    http_respond(client_soc, &req, http_err, "text/plain", "An error occurred parsing the request.");
    return NULL;
  }

  handle_routing(client_soc, &req);
  return NULL;
}

void handle_routing(int client_soc, HttpRequest* req) {
  if (http_route_get(req, "/")) {
    http_respond(client_soc, req, "200 OK", "text/plain", "Server is up and running!");
    return;
  }
  if (http_route_get(req, "/json")) {
    http_respond(client_soc, req, "200 OK", "application/json", "{\"status\": \"success\"}");
    return;
  }

  http_respond(client_soc, req, "404 Not Found", "text/plain", "Route not found.");
}
