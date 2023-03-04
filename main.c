#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#define SV_IMPLEMENTATION
#include "sv.h"
#include "http_status_code.h"

#define ENDL "\r\n"
#define handle_error(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

String_View cwd = {0};

int init_socket(int port) {
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

  if (listen(s, 50) == -1)
    handle_error("listen");

  return s;
}

enum HTTPVerb {
  GET,
  HEAD,
  POST,
  PUT,
  DELETE
};

struct HTTPHeader {
  enum HTTPVerb verb;
  String_View path;
  String_View version;
  String_View hFrom;
  String_View hHost;
  String_View hUseragent;
  String_View hAccept;
  String_View hConnection;
};

void send_http_error(const int clientfd, enum HTTPSTATUSCODES code) {
  struct http_status_code_s code_info = http_status_codes[code];
  dprintf(clientfd, "HTTP/1.1 %s %s"ENDL"Connection: close"ENDL ENDL, code_info.scode, code_info.text);
}

bool send_file(const int clientfd, const char* path, ssize_t size) {
  int filefd = open(path, O_RDONLY | O_NOFOLLOW | O_NOATIME);
  if (filefd == -1) {
    perror("open");
    goto clean;
  }

  ssize_t sent = sendfile(clientfd, filefd, NULL, size);
  if (sent == -1) {
    perror("sendfile");
    goto clean;
  }
  return true;

 clean:
  close(clientfd);
  return false;
}

bool consume_HTTP_header(String_View* svbuf, struct HTTPHeader* header) {
  int nb_line = 0;
  String_View line = SV_NULL;
  bool end_of_header = false;

  line = sv_chop_by_sv(svbuf, SV(ENDL));
  while (! sv_eq(line, SV(""))) {
    nb_line = nb_line + 1;

    if (nb_line == 1) {
      String_View verb = sv_chop_by_delim(&line, ' ');
      
      if (sv_eq(verb, SV("GET"))) {
        header->verb = GET;
      } else if (sv_eq(verb, SV("HEAD"))) {
        header->verb = HEAD;
      } else if (sv_eq(verb, SV("POST"))) {
        header->verb = POST;
      } else if (sv_eq(verb, SV("PUT"))) {
        header->verb = PUT;
      } else if (sv_eq(verb, SV("DELETE"))) {
        header->verb = DELETE;
      } else {
        printf("ERROR: Unsupported verb " SV_Fmt "\n", SV_Arg(verb));
      }

      // TODO: check for too long path >= 65537
      // TODO: strip extra /'/'
      header->path = sv_chop_by_delim(&line, ' '); 
      header->version = sv_chop_by_delim(&line, ' '); // TODO: html/0 only has GET verb
      header->path = sv_trim_right(header->path); // remove trailling space in case of HTTP/1.0
    } else {
      String_View header_key = sv_chop_by_sv(&line, SV(": "));
      String_View header_value = line;

      if (sv_eq_ignorecase(header_key, SV("from"))) {
        header->hFrom = header_value;
      } else if (sv_eq_ignorecase(header_key, SV("host"))) {
        header->hHost = header_value;
      } else if (sv_eq_ignorecase(header_key, SV("user-agent"))) {
        header->hUseragent = header_value;
      } else if (sv_eq_ignorecase(header_key, SV("accept"))) {
        header->hAccept = header_value;
      } else if (sv_eq_ignorecase(header_key, SV("connection"))) {
        header->hConnection = header_value;
      } else {
        printf("ignored header: " SV_Fmt "\n", SV_Arg(header_key));
      }
    }

    line = sv_chop_by_sv(svbuf, SV("\r\n"));
    if (sv_starts_with(line, SV("\r\n"))) {
      sv_chop_by_sv(svbuf, SV("\r\n"));
      end_of_header = true;
      break;
    }
  }
  printf("path: " SV_Fmt "\n", SV_Arg(header->path));
  printf("version: " SV_Fmt "\n", SV_Arg(header->version));

  return end_of_header;
}


void handle_connection(int clientfd, struct sockaddr* client, socklen_t client_len) {
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    
    if (getnameinfo(client, client_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
      printf("host=%s, serv=%s\n", hbuf, sbuf);
    else
      printf("could not getnameinfo\n");


    char buff[1500] = {0};

    int nb = read(clientfd, buff, 1499);
    buff[nb + 1] = '\0';
    
    struct HTTPHeader header = {0};
    String_View svbuf = sv_from_parts(buff, nb);
    if (! consume_HTTP_header(&svbuf, &header)) {
      send_http_error(clientfd, HTTPSC_RequestHeaderFieldsTooLarge);
      return;
    }
    printf("rest: " SV_Fmt "\n", SV_Arg(svbuf));

    if (header.path.count > 1) {
      char path[header.path.count];
      memcpy(path, header.path.data + 1, header.path.count - 1);
      path[header.path.count - 1] = '\0';
      // TODO: remove query parameters '?' '#'
      // TODO: redirect if path ends with /
      // TODO: look for index.html

      char* resolved_path = NULL;
      resolved_path = realpath(path, NULL);
      if (resolved_path == NULL) {
        if (errno == ENOENT) {
          send_http_error(clientfd, HTTPSC_NotFound);
        } else if (errno == EACCES) {
          send_http_error(clientfd, HTTPSC_Forbidden);
          perror("realpath");
        } else {
          send_http_error(clientfd, HTTPSC_InternalServerError);
          perror("realpath");
        }
        goto end;
      }

      if (strncmp(cwd.data, resolved_path, cwd.count) != 0) {
        // hide files not in working directory
        send_http_error(clientfd, HTTPSC_NotFound);
        goto end;
      }

      char* web_path = resolved_path + cwd.count;

      struct stat statbuf;
      if (stat(resolved_path, &statbuf) == -1) {
        perror("fstat");
        goto end;
      }

      if (S_ISREG(statbuf.st_mode)) {
        char header2[] = "HTTP/1.1 200 OK"ENDL
          "Server: c"ENDL
          "Cache-Control: no-cache"ENDL
          "Content-Type: text/html"ENDL
          "Connection: Closed"ENDL ENDL;
        write(clientfd, header2, sizeof(header2));
        send_file(clientfd, resolved_path, statbuf.st_size);
      } else if (S_ISDIR(statbuf.st_mode)) {
        struct dirent* dir_entry;
        DIR* dir = opendir(resolved_path);
        if (dir == NULL) {
          perror("opendir");
          send_http_error(clientfd, HTTPSC_InternalServerError);
          goto end;
        }

        char header2[] = "HTTP/1.1 200 OK"ENDL
          "Server: c"ENDL
          "Cache-Control: no-cache"ENDL
          "Content-Type: text/html"ENDL
          "Connection: Closed"ENDL ENDL;
        write(clientfd, header2, sizeof(header2));

        dprintf(clientfd, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body><h1>Directory listing</h1>");
        while ((dir_entry = readdir(dir)) != NULL) {
          dprintf(clientfd, "<a href=\"%s/%s\">%s</a><br/>", web_path, dir_entry->d_name, dir_entry->d_name); //TODO: urlencode, full path
        }
        
        dprintf(clientfd, "</body></html>");        
        closedir(dir);
      }

      if (resolved_path != NULL)
        free(resolved_path);
    end:
    }
}

int main() {
  int port = 8080;
  int s = init_socket(port);

  char* temp_cwd = get_current_dir_name();
  cwd = sv_from_cstr(temp_cwd);

  printf("Serving files in \"%s\"\n", temp_cwd);
  printf("Listening on port %d\n", port);

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int c = accept(s, (struct sockaddr*) &client_addr, &len);
    if (c == -1)
      handle_error("accept");

    handle_connection(c, (struct sockaddr*) &client_addr, len);
      
    close(c);
  }
  
  printf("End\n");
  free(temp_cwd);
  return EXIT_SUCCESS;
}
