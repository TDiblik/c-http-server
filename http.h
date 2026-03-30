#pragma once

// ============================================================================
// DEFAULTS
// ============================================================================
#ifndef HTTP_IMPLEMENTATION_LOG_IP
#define HTTP_IMPLEMENTATION_LOG_IP false
#endif
#ifndef HTTP_IMPLEMENTATION_MAX_REQ_HEADERS_LEN
#define HTTP_IMPLEMENTATION_MAX_REQ_HEADERS_LEN 8192 // 8KB; same as apache tomcat 6 (https://www.geekersdigest.com/max-http-request-header-size-server-comparison/)
#endif
#ifndef HTTP_IMPLEMENTATION_MAX_REQ_BODY_LEN
#define HTTP_IMPLEMENTATION_MAX_REQ_BODY_LEN 8388608 // 8MB
#endif
#ifndef HTTP_IMPLEMENTATION_MAX_FILE_RESP_BODY_LEN
#define HTTP_IMPLEMENTATION_MAX_FILE_RESP_BODY_LEN 1048576 // 1MB
#endif

// ============================================================================
// DEFINITIONS SECTION
// ============================================================================
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

int http_soc_setup(void);
int http_soc_listen(int server_soc, int port);
ssize_t http_soc_send(int socket, const void* message, size_t message_len, int flags);
int http_soc_close(int socket);
int http_client_accept(int server_soc);

typedef enum { HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS, HTTP_HEAD, HTTP_UNKNOWN } HttpMethod;
typedef struct {
  HttpMethod method;
  char _method_raw[16];
  char path[256];
  char protocol[16];
  char* headers;
  size_t headers_len;
  char* body;
  size_t body_len;
} HttpRequest;
void http_request_free(HttpRequest* req);

int http_parse_request(int client_soc, HttpRequest* req);
int http_get_header_value(HttpRequest* req, char* header_name, char** header_value, size_t* header_value_len);

bool http_route_get(HttpRequest* req, const char* path);
bool http_route_post(HttpRequest* req, const char* path);
bool http_route_put(HttpRequest* req, const char* path);
bool http_route_delete(HttpRequest* req, const char* path);
bool http_route_options(HttpRequest* req, const char* path);
bool http_route_head(HttpRequest* req, const char* path);

void http_respond(int client_soc, HttpRequest* req, const char* status, const char* content_type, const char* body);
void http_respond_with_file(int client_soc, HttpRequest* req, char* file_path, char* file_type);
#define http_respond_404(client_soc, req) http_respond(client_soc, req, "404 Not Found", "text/plain", "Route not found.");
#define http_respond_500(client_soc, req) http_respond(client_soc, req, "500 Internal Server Error", "text/plain", "An internal server error occured.");

// ============================================================================
// IMPLEMENTATION SECTION
// ============================================================================
#ifdef HTTP_IMPLEMENTATION
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;

int http_soc_setup(void) {
  int server_soc = socket(AF_INET, SOCK_STREAM, 0);
  if (server_soc < 0) {
    perror("server_soc creation failed");
    return -1;
  }

  int opt = 1;
  if (setsockopt(server_soc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("server_soc setsockopt failed");
    http_soc_close(server_soc);
    return -1;
  }

  return server_soc;
}

int http_soc_listen(int server_soc, int port) {
  sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons((uint16_t)port);

  if (bind(server_soc, (sockaddr*)&address, sizeof(address)) < 0) {
    perror("server_soc bind failed");
    http_soc_close(server_soc);
    return -1;
  }
  if (listen(server_soc, 10) < 0) {
    perror("server_soc listen failed");
    http_soc_close(server_soc);
    return -1;
  }

  return 0;
}

ssize_t http_soc_send(int socket, const void* message, size_t message_len, int flags) {
  return send(socket, message, message_len, flags);
}

int http_soc_close(int server_soc) {
  return close(server_soc);
}

int http_client_accept(int server_soc) {
  struct sockaddr_in client_address = {0};
  socklen_t client_address_len = sizeof(client_address);
  int client_soc = accept(server_soc, (sockaddr*)&client_address, &client_address_len);
  if (client_soc < 0) return -1;

  if (HTTP_IMPLEMENTATION_LOG_IP) {
    char ip_str[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &client_address.sin_addr, ip_str, INET_ADDRSTRLEN) != NULL) printf("client_soc accepted connection from %s\n", ip_str);
  }

  return client_soc;
}

void http_request_free(HttpRequest* req) {
  if (!req) return;
  free(req->headers);
  free(req->body);
  req->headers = NULL;
  req->body = NULL;
}

int http_parse_request(int client_soc, HttpRequest* req) {
  // read request line
  char req_line[512] = {0};
  size_t req_line_len = 0;
  bool req_line_ok = false;
  while (req_line_len < sizeof(req_line) - 1) {
    if (read(client_soc, req_line + req_line_len, 1) <= 0) return -2;
    req_line_len += 1;
    if (req_line_len >= 2 && req_line[req_line_len - 2] == '\r' && req_line[req_line_len - 1] == '\n') {
      req_line[req_line_len] = '\0';
      req_line_ok = true;
      break;
    }
  }
  if (!req_line_ok) return -3;
  if (sscanf(req_line, "%15s %255s %15s", req->_method_raw, req->path, req->protocol) != 3) return -3;

  req->method = HTTP_UNKNOWN;
  if (strcmp(req->_method_raw, "GET") == 0) req->method = HTTP_GET;
  else if (strcmp(req->_method_raw, "POST") == 0) req->method = HTTP_POST;
  else if (strcmp(req->_method_raw, "PUT") == 0) req->method = HTTP_PUT;
  else if (strcmp(req->_method_raw, "DELETE") == 0) req->method = HTTP_DELETE;
  else if (strcmp(req->_method_raw, "OPTIONS") == 0) req->method = HTTP_OPTIONS;
  else if (strcmp(req->_method_raw, "HEAD") == 0) req->method = HTTP_HEAD;

  // read headers
  bool headers_ok = false;
  req->headers_len = 0;
  req->headers = malloc(HTTP_IMPLEMENTATION_MAX_REQ_HEADERS_LEN + 1);
  if (req->headers == NULL) return -1;
  while (req->headers_len < HTTP_IMPLEMENTATION_MAX_REQ_HEADERS_LEN) {
    if (read(client_soc, req->headers + req->headers_len, 1) <= 0) return -2;
    req->headers_len += 1;
    if (req->headers_len >= 4 && req->headers[req->headers_len - 4] == '\r' && req->headers[req->headers_len - 3] == '\n' && req->headers[req->headers_len - 2] == '\r' && req->headers[req->headers_len - 1] == '\n') {
      req->headers[req->headers_len] = '\0';
      req->headers_len += 1;
      char* resized = realloc(req->headers, req->headers_len);
      if (resized != NULL) {
        req->headers = resized;
      }
      headers_ok = true;
      break;
    }
  }
  if (!headers_ok) return -4;

  // read body
  char* content_len_raw = NULL;
  size_t content_len_raw_len = 0;
  int err_code = http_get_header_value(req, "Content-Length", &content_len_raw, &content_len_raw_len);
  if (err_code < 0) { free(content_len_raw); return err_code; }
  if (content_len_raw_len == 0) return 0;

  int parsed_len = atoi(content_len_raw);
  free(content_len_raw);

  if (parsed_len < 0) return -4;
  if (parsed_len == 0) return 0;
  if (parsed_len > HTTP_IMPLEMENTATION_MAX_REQ_BODY_LEN) return -5;

  size_t content_len = (size_t)parsed_len;
  req->body = (char*)malloc(content_len+1);
  size_t body_bytes_read = 0;
  while (body_bytes_read < content_len) {
      ssize_t bytes_read = read(client_soc, req->body + body_bytes_read, content_len - body_bytes_read);
      if (bytes_read <= 0) return -6;
      body_bytes_read += (size_t)bytes_read;
  }
  req->body[body_bytes_read] = '\0';
  req->body_len = body_bytes_read;

  return 0;
}

int http_get_header_value(HttpRequest* req, char* header_name, char** header_value, size_t* header_value_len) {
  if (!req || !req->headers || !header_name || !header_value) return -1;

  char *header_start = strstr(req->headers, header_name);
  if (!header_start) return 0;

  char *end_of_line = strstr(header_start, "\r\n");
  if (!end_of_line) return -4;

  char *colon = strchr(header_start, ':');
  if (!colon || colon > end_of_line) return -4;

  char *value_start = colon + 1;
  while (*value_start == ' ' && value_start < end_of_line) value_start++;

  *header_value_len = (size_t)(end_of_line - value_start);
  *header_value = (char*)malloc(*header_value_len + 1);
  if (!*header_value) return -1;

  memcpy(*header_value, value_start, *header_value_len);
  (*header_value)[*header_value_len] = '\0';

  return 0;
}

bool http_route_get(HttpRequest* req, const char* path) { return req->method == HTTP_GET && strcmp(req->path, path) == 0; }
bool http_route_post(HttpRequest* req, const char* path) { return req->method == HTTP_POST && strcmp(req->path, path) == 0; }
bool http_route_put(HttpRequest* req, const char* path) { return req->method == HTTP_PUT && strcmp(req->path, path) == 0; }
bool http_route_delete(HttpRequest* req, const char* path) { return req->method == HTTP_DELETE && strcmp(req->path, path) == 0; }
bool http_route_options(HttpRequest* req, const char* path) { return req->method == HTTP_OPTIONS && strcmp(req->path, path) == 0; }
bool http_route_head(HttpRequest* req, const char* path) { return req->method == HTTP_HEAD && strcmp(req->path, path) == 0; }

void http_respond(int client_soc, HttpRequest* req, const char* status, const char* content_type, const char* body) {
  if (!status) status = "200 OK";
  if (!content_type) content_type = "text/plain";
  if (!body) body = "";

  char headers[1024];
  size_t body_len = strlen(body);
  snprintf(headers, sizeof(headers),
    "HTTP/1.1 %s\r\n"
    "Connection: close\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %zu\r\n"
    "\r\n",
    status, content_type, body_len
  );

  http_soc_send(client_soc, headers, strlen(headers), 0);
  if (body_len > 0) {
    http_soc_send(client_soc, body, body_len, 0);
  }
  http_soc_close(client_soc);
  http_request_free(req);
}

void http_respond_with_file(int client_soc, HttpRequest* req, char* file_path, char* file_type) {
  FILE *fptr = fopen(file_path, "r");
  if (fptr == NULL) { http_respond_404(client_soc, req); return; }

  char* file_contents = calloc(1, HTTP_IMPLEMENTATION_MAX_FILE_RESP_BODY_LEN);
  if (file_contents == NULL) {
    fclose(fptr);
    http_respond_500(client_soc, req);
    return;
  }

  size_t bytes_read = fread(file_contents, 1, HTTP_IMPLEMENTATION_MAX_FILE_RESP_BODY_LEN - 1, fptr);
  fclose(fptr);

  if (bytes_read == 0 && ferror(fptr)) {
    free(file_contents);
    http_respond_404(client_soc, req);
    return;
  }
  http_respond(client_soc, req, "200 OK", file_type, file_contents);
  free(file_contents);
}

#endif
