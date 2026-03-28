#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define PORT 8888
#define LOG_IP true
#define MAX_HEADERS_LEN 8192 // 8KB; same as apache tomcat 6 (https://www.geekersdigest.com/max-http-request-header-size-server-comparison/)
#define MAX_BODY_LEN 8388608 // 8MB

// note on usage: we can safely convert sockaddr_in into sockaddr, since they are 1:1 aligned in memory
typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
typedef struct {
    char method[16];
    char path[256];
    char protocol[16];
    char* headers;
    size_t headers_len;
    char* body;
    size_t body_len;
} HttpRequest;

#define SEND_RESPONSE(client_soc, response, req) do { \
    send(client_soc, response, strlen(response), 0);  \
    close(client_soc);                                \
    free(req.headers);                                \
    free(req.body);                                   \
    (req.headers) = NULL;                             \
    (req.body) = NULL;                                \
} while (0)

void handle_client(int client_soc);
int _read_into_req(int client_soc, HttpRequest* req);
int get_header_value(HttpRequest* req, char* header_name, char** header_value, size_t* header_value_len);

int main(void) {
  int server_soc = socket(AF_INET, SOCK_STREAM, 0);
  if (server_soc < 0) {
    perror("server_soc creation failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  if (setsockopt(server_soc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("server_soc setsockopt failed");
    close(server_soc);
    exit(EXIT_FAILURE);
  }

  sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_soc, (sockaddr*)&address, sizeof(address)) < 0) {
    perror("server_soc bind failed");
    close(server_soc);
    exit(EXIT_FAILURE);
  }
  if (listen(server_soc, 10) < 0) {
    perror("server_soc listen failed");
    close(server_soc);
    exit(EXIT_FAILURE);
  }

  printf("Server started at http://localhost:%d\n", PORT);
  while (1) {
    struct sockaddr_in client_adress = {0};
    socklen_t client_adress_len = sizeof(client_adress);
    int client_soc = accept(server_soc, (sockaddr*)&client_adress, &client_adress_len);
    if (client_soc < 0) {
      perror("client_soc accept failed");
      continue;
    }

    if (LOG_IP) {
      char ip_str[INET_ADDRSTRLEN] = {0};
      if (inet_ntop(AF_INET, &client_adress.sin_addr, ip_str, INET_ADDRSTRLEN) != NULL) {
        printf("client_soc accepted connection from %s\n", ip_str);
      }
    }

    handle_client(client_soc);
    close(client_soc);
  }

  close(server_soc);
  return 0;
}

void handle_client(int client_soc) {
  HttpRequest req = {0};

  char response[8192];
  int parse_err_code = _read_into_req(client_soc, &req);

  const char *http_err = NULL;
  switch (parse_err_code) {
    case 0:
      break;
    case -1:
      perror("Server Error: Allocation issue occurred");
      http_err = "500 Internal Server Error";
      break;
    case -2:
      perror("Server Error: Socket read issue");
      http_err = "500 Internal Server Error";
      break;
    case -3:
      fprintf(stderr, "Client Error: Malformed request line\n");
      http_err = "400 Bad Request";
      break;
    case -4:
      fprintf(stderr, "Client Error: Malformed HTTP header(s)\n");
      http_err = "400 Bad Request";
      break;
    case -5:
      fprintf(stderr, "Client Error: Request body too large\n");
      http_err = "413 Payload Too Large";
      break;
    case -6:
      fprintf(stderr, "Client Error: Malformed request body\n");
      http_err = "400 Bad Request";
      break;
    default:
      fprintf(stderr, "Server Error: Unknown internal error code: %d\n", parse_err_code);
      http_err = "500 Internal Server Error";
      break;
  }
  if (http_err != NULL) {
    sprintf(response, "HTTP/1.1 %s\r\nConnection: close\r\n\r\n", http_err);
    SEND_RESPONSE(client_soc, response, req);
    return;
  }

  SEND_RESPONSE(client_soc, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", req);
}

int _read_into_req(int client_soc, HttpRequest* req) {
  // read request line
  char req_line[512] = {0}; // only 288 bytes should be used in reality, but I want some room for error
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
  if (sscanf(req_line, "%15s %255s %15s", req->method, req->path, req->protocol) != 3) return -3;

  // read headers
  bool headers_ok = false;
  req->headers_len = 0;
  req->headers = malloc(MAX_HEADERS_LEN + 1);
  if (req->headers == NULL) return -1;
  while (req->headers_len < MAX_HEADERS_LEN) {
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
  int err_code = get_header_value(req, "Content-Length", &content_len_raw, &content_len_raw_len);
  if (err_code < 0) { free(content_len_raw); return err_code; }
  if (content_len_raw_len == 0) return 0;

  int parsed_len = atoi(content_len_raw);
  free(content_len_raw);

  if (parsed_len < 0) return -4;
  if (parsed_len == 0) return 0;
  if (parsed_len > MAX_BODY_LEN) return -5;

  size_t content_len = (size_t)parsed_len; // safe to do since int < long and we guard against negative numbers above
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

// Caller is reponsible for freeing the header_value after usage
int get_header_value(HttpRequest* req, char* header_name, char** header_value, size_t* header_value_len) {
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
