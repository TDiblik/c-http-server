#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 80

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
void handle_client(int client_soc, sockaddr_in* client_adress, socklen_t* client_adress_len);

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
    handle_client(client_soc, &client_adress, &client_adress_len);
    close(client_soc);
  }

  close(server_soc);
  return 0;
}

void handle_client(int client_soc, sockaddr_in* client_adress, socklen_t* client_adress_len) {
    // todo: implemnet, too tired to do it now.
}
