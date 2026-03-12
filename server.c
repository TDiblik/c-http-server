#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define PORT 80
#define LOG_IP true
#define MAX_HEADERS_LEN 8192 // same as apache tomcat 6 (https://www.geekersdigest.com/max-http-request-header-size-server-comparison/)

// note on usage: we can safely convert sockaddr_in into sockaddr, since they are 1:1 aligned in memory
typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
typedef struct {
    char method[16];
    char path[256];
    char protocol[16];
    char *body;
} HttpRequest;

void handle_client(int client_soc);
int read_protocol_info(int client_soc, HttpRequest* req);

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
      char ip_str[INET_ADDRSTRLEN];
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
  switch (read_protocol_info(client_soc, &req)) {
    case -1:
      perror("client didn't send protocol info bytes. closing.");
      return;
  };

  printf("%s %s: %s\n", req.protocol, req.method, req.path);
}

int read_protocol_info(int client_soc, HttpRequest* req) {
  size_t protocol_info_buf_len = sizeof(req->method) + sizeof(req->path) + sizeof(req->protocol);
  char protocol_info_buf[protocol_info_buf_len];
  ssize_t bytes_read = read(client_soc, &protocol_info_buf, protocol_info_buf_len-1);
  if (bytes_read < 0) {
    return -1;
  }
  protocol_info_buf[bytes_read] = '\0';
  sscanf(protocol_info_buf, "%15s %255s %15s", req->method, req->path, req->protocol);
  return 0;
}

// void read_headers(int client_soc) {
//   // int total_bytes = 0;
//   // int n;
//   // while (total_bytes < MAX_HEADERS_LEN) {
//   //   n = read(client_soc, )
//   // }
//   // todo: implement
// }

// void read_body() {
//   // todo: implement
// }
