
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "threadpool.h"
#include "http.h"
#define SV_IMPLEMENTATION
#include "sv.h"

#define ENDL "\r\n"
#define handle_error(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define NB_CLIENT_MAX 1000

String_View current_working_directory = {0};

static int init_socket(int port) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1)
    handle_error("socket");

  const int enable = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1)
    handle_error("setsockopt(SO_REUSEADDR) failed");
  
  struct sockaddr_in my_addr = {
    .sin_family = AF_INET,
    .sin_port   = htons(port),
    .sin_addr   = {
      htonl(INADDR_ANY)
    }
  };

  if (bind(s, (struct sockaddr*) &my_addr, sizeof(my_addr)) == -1)
    handle_error("bind");

  if (listen(s, NB_CLIENT_MAX) == -1)
    handle_error("listen");

  return s;
}

static void child_main(struct HTTP_Connection* conn) {
  http_handle_connection(conn);
  close(conn->client_fd);

  free(conn);
}

int main(void) {
  int port = 8080;
  int s = init_socket(port);

  char* temp_cwd = get_current_dir_name();
  current_working_directory = sv_from_cstr(temp_cwd);

  printf("Serving files in \"%s\"\n", temp_cwd);
  printf("Listening on port http://0.0.0.0:%d/\n", port);

  threadpool_t* tp = threadpool_create(5, NB_CLIENT_MAX, 0);
  
  while (1) {
    struct HTTP_Connection* conn = calloc(1, sizeof(struct HTTP_Connection));
    struct sockaddr_in client_addr = {0};
    conn->client_addr_len = sizeof(client_addr);

    conn->client_fd = accept(s, (struct sockaddr*) &client_addr, &(conn->client_addr_len));
    if (conn->client_fd == -1)
      handle_error("accept");
    
    conn->client_addr = (struct sockaddr*)&client_addr;
    conn->close_on_first_responce = true;

    int getnameinfo_ret = getnameinfo(conn->client_addr, conn->client_addr_len, conn->hbuf, sizeof(conn->hbuf), conn->sbuf, sizeof(conn->sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
    if (getnameinfo_ret != 0)
      printf("could not getnameinfo: %s\n", gai_strerror(getnameinfo_ret));

    if (threadpool_add(tp, (void (*)(void *))child_main, conn, 0) < 0){
      fprintf(stderr, "threadpool_add error");
      return EXIT_FAILURE;
    }
  }
  
  printf("End\n");
  free(temp_cwd);
  return EXIT_SUCCESS;
}
