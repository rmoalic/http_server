#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#include "http_status_code.h"
#include "http.h"

static int decodeURIComponent (char *sSource, char *sDest) { // https://stackoverflow.com/a/20437049
  assert(sSource != NULL);
  assert(sDest != NULL);
  int nLength;
  for (nLength = 0; *sSource; nLength++) {
    if (*sSource == '%' && sSource[1] && sSource[2] && isxdigit(sSource[1]) && isxdigit(sSource[2])) {
      sSource[1] -= sSource[1] <= '9' ? '0' : (sSource[1] <= 'F' ? 'A' : 'a')-10;
      sSource[2] -= sSource[2] <= '9' ? '0' : (sSource[2] <= 'F' ? 'A' : 'a')-10;
      sDest[nLength] = 16 * sSource[1] + sSource[2];
      sSource += 3;
      continue;
    }
    sDest[nLength] = *sSource++;
  }
  sDest[nLength] = '\0';
  return nLength;
}

#define implodeURIComponent(url) decodeURIComponent(url, url)

static int http_send_status_line(struct HTTP_Connection* conn, struct HTTP_Response* resp) {
  #define buff_size 50
  if (resp->header_sent) {
    fprintf(stderr, "err: http_send_status_name called after headers sent\n");
    return -1;
  }
  if (resp->status_line_sent) {
    fprintf(stderr, "err: http_send_status_name called more than one time\n");
    return -1;    
  }

  char buff[buff_size] = {0};

  assert(resp->response_code < HTTPSC_LAST_VALUE);
  struct http_status_code_s code = http_status_codes[resp->response_code];
  
  size_t buff_n = snprintf(buff, buff_size, "HTTP/1.1 %s %s" HTTP_ENDL, code.scode, code.text);

  ssize_t ret = send(conn->client_fd, buff, buff_n, MSG_MORE);
  if (ret == -1) {
    perror("send");
    return -1;
  }

  return 0;
  #undef buff_size
}

static int http_add_header(struct HTTP_Response* resp, char* name, String_View value) {
  if (resp->header_sent) {
    fprintf(stderr, "err: http_add_header called after headers sent: %s\n", name);
    return -1;
  }
  size_t name_len = strlen(name);
  if (name_len > HTTP_HEADER_NAME_MAX_LEN) {
    return -1;
  }

  size_t header_len = name_len + sizeof(HTTP_HEADER_SEPARATOR) + value.count + sizeof(HTTP_ENDL) - 2;
  if ((resp->header_len + header_len) > (HTTP_HEADER_MAX_LEN - sizeof(HTTP_ENDL) - 1)) {
    return -1;
  }

  char* buff = resp->header + resp->header_len;

  memcpy(buff, name, name_len);
  buff += name_len;
  memcpy(buff, HTTP_HEADER_SEPARATOR, sizeof(HTTP_HEADER_SEPARATOR) - 1);
  buff += sizeof(HTTP_HEADER_SEPARATOR) - 1;
  memcpy(buff, value.data, value.count);
  buff += value.count;
  memcpy(buff, HTTP_ENDL, sizeof(HTTP_ENDL) - 1);
  buff += sizeof(HTTP_ENDL) - 1;

  resp->header_len += header_len;

  return 0;
}

static int http_send_headers(struct HTTP_Connection* conn, struct HTTP_Response* resp) {
  if (resp->header_sent) {
    return -1;
  }

  if (! resp->status_line_sent) {
    if (http_send_status_line(conn, resp) == -1) {
      return -1;
    }
  }

  if (resp->header_len + (sizeof(HTTP_ENDL) - 1) > HTTP_HEADER_MAX_LEN) {
    return -1;
  }

  int flags = 0;
  if (! resp->header_only) {
    flags |= MSG_MORE;
  }
  
  char* buff = resp->header + resp->header_len;
  memcpy(buff, HTTP_ENDL, sizeof(HTTP_ENDL) - 1);
  resp->header_len += sizeof(HTTP_ENDL) - 1;
  
  ssize_t ret = send(conn->client_fd, resp->header, resp->header_len, flags);
  if (ret == -1) {
    perror("send");
    return -1;
  }
  resp->header_sent = true;
  return 0;
}


static bool consume_HTTP_header(String_View* svbuf, struct HTTP_Request* header) {
  int nb_line = 0;
  String_View line = SV_NULL;
  bool end_of_header = false;

  line = sv_chop_by_sv(svbuf, SV(HTTP_ENDL));
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
      } else if (sv_eq_ignorecase(header_key, SV("if-modified-since"))) {
        header->hIfModifiedSince = header_value;
      } else {
        //printf("ignored header: " SV_Fmt "\n", SV_Arg(header_key));
      }
    }

    line = sv_chop_by_sv(svbuf, SV("\r\n"));
    if (sv_starts_with(line, SV("\r\n"))) {
      sv_chop_by_sv(svbuf, SV("\r\n"));
      end_of_header = true;
      break;
    }
  }

  return end_of_header;
}

static void apache2_log_response(struct HTTP_Connection* conn, struct HTTP_Request* req, struct HTTP_Response* resp) {
  time_t timestamp = time(NULL);
  struct tm * now = localtime(&timestamp);
  char time_buff[30];
  strftime(time_buff, 29, "%d/%b/%Y:%H:%M:%S %z", now);

  assert(resp->response_code < HTTPSC_LAST_VALUE);
  struct http_status_code_s code = http_status_codes[resp->response_code];
  
  printf("%s - [%s] \"%d " SV_Fmt " " SV_Fmt "\" %s %zu\n", conn->hbuf, time_buff, req->verb, SV_Arg(req->path), SV_Arg(req->version), code.scode, resp->response_len);
}

static void http_send_error(struct HTTP_Connection* conn, struct HTTP_Response* resp) {
  http_add_header(resp, "Content-Type", SV("text/plain"));
  resp->header_only = false;
  if (http_send_headers(conn, resp) == -1) {
    fprintf(stderr, "Error: http_send_error did not complete successfully\n");
    return;
  }

  assert(resp->response_code < HTTPSC_LAST_VALUE);
  struct http_status_code_s code = http_status_codes[resp->response_code];

  ssize_t s = send(conn->client_fd, code.text, strlen(code.text), 0);
  if (s == -1) {
    fprintf(stderr, "Error: http_send_error send\n");
    return;
  }
  resp->response_len = s;
}

static int send_file(const int clientfd, const char* path, ssize_t size) {
  int filefd = open(path, O_RDONLY | O_NOFOLLOW | O_NOATIME);
  if (filefd == -1) {
    perror("open");
  }

  ssize_t sent = sendfile(clientfd, filefd, NULL, size);
  if (sent == -1) {
    perror("sendfile");
    close(filefd);
    return -1;
  }
  close(filefd);
  return sent;
}

static void http_serve_file(struct HTTP_Connection* conn, struct HTTP_Request* req, struct HTTP_Response* resp, const char* path, struct stat filestat) {
  http_add_header(resp, "Connection", SV("Closed"));

  struct tm last_modified_time_from_req = {0};
  if (req->hIfModifiedSince.count > 0) {
    char* cread = strptime(req->hIfModifiedSince.data, "%a, %d %b %Y %H:%M:%S %Z", &last_modified_time_from_req);
    if (cread != NULL && req->hIfModifiedSince.data + req->hIfModifiedSince.count == cread) {
      if (filestat.st_mtim.tv_sec <= mktime(&last_modified_time_from_req) - timezone) {
        resp->response_code = HTTPSC_NotModified;
        resp->header_only = true;
        http_send_headers(conn, resp);
        return;
      }
    }
  }

  resp->response_code = HTTPSC_OK;
  http_add_header(resp, "Cache-control", SV("public"));
  http_add_header(resp, "Content-Type", SV("text/plain"));

  struct tm * mtim = gmtime(&(filestat.st_mtim.tv_sec));
  char content_size_str[20];
  sprintf(content_size_str, "%zu", filestat.st_size);
  http_add_header(resp, "Content-Length", sv_from_cstr(content_size_str));


  char time_buff[40];
  strftime(time_buff, 39, "%a, %d %b %Y %H:%M:%S %Z", mtim);
  http_add_header(resp, "Last-Modified", sv_from_cstr(time_buff));

  http_send_headers(conn, resp);
      
  ssize_t s = send_file(conn->client_fd, path, filestat.st_size);
  if (s == -1) {
    return;
  };
  resp->response_len = s;
}

static void http_serve_directory(struct HTTP_Connection* conn, struct HTTP_Request* req, struct HTTP_Response* resp, const char* path, const char* web_path) {
  struct dirent* dir_entry;
  DIR* dir = opendir(path);
  if (dir == NULL) {
    perror("opendir");
    resp->response_code = HTTPSC_InternalServerError;
    http_send_error(conn, resp);
    return;
  }

  //TODO: file name encoding

  resp->response_code = HTTPSC_OK;
  http_add_header(resp, "Cache-control", SV("no-cache"));
  http_add_header(resp, "Content-Type", SV("text/html"));
  http_add_header(resp, "Connection", SV("Closed"));

  http_send_headers(conn, resp);

  size_t s = 0;
  s += dprintf(conn->client_fd, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body><h1>Directory listing for %s</h1><hr>", web_path);
  while ((dir_entry = readdir(dir)) != NULL) {
    s+= dprintf(conn->client_fd, "<a href=\"%s/%s\">%s</a><br/>", web_path, dir_entry->d_name, dir_entry->d_name); //TODO: urlencode, full path
  }
        
  s += dprintf(conn->client_fd, "<hr></body></html>");
  resp->response_len = s;
  closedir(dir);
}

void http_handle_connection(struct HTTP_Connection* conn) {
  char buff[HTTP_HEADER_MAX_LEN] = {0};

  int nb = recv(conn->client_fd, buff, HTTP_HEADER_MAX_LEN - 1, 0);
  if (nb == -1) {
    return;
  }
  buff[nb + 1] = '\0';
    
  struct HTTP_Request request = {0};
  struct HTTP_Response response = {0};

  http_add_header(&response, "Server", SV("http_server"));

  time_t timestamp = time(NULL);
  struct tm * now = gmtime(&timestamp);
  char time_buff[40];
  strftime(time_buff, 39, "%a, %d %b %Y %H:%M:%S %Z", now);

  http_add_header(&response, "Date", sv_from_cstr(time_buff));

  String_View svbuf = sv_from_parts(buff, nb);
  if (! consume_HTTP_header(&svbuf, &request)) {
    response.response_code = HTTPSC_RequestHeaderFieldsTooLarge;
    http_send_error(conn, &response);

    apache2_log_response(conn, &request, &response);
    return;
  }

  if (svbuf.data[0] != '\0') { // TODO: fix sketchy length
    printf("rest: " SV_Fmt "\n", SV_Arg(svbuf));
  }

  char path[request.path.count + 1];
  memcpy(path, request.path.data, request.path.count);
  path[request.path.count] = '\0';

  char* wpath;
  if (request.path.count > 1) {
    //TODO: anchor parsing
    char* anchor = strchr(path, '#');
    if (anchor != NULL) {
      *anchor = '\0';
    }

    //TODO: query parameters parsing
    char* query  = strchr(path, '?');
    if (query != NULL) {
      *query = '\0';
    }

    size_t path_len = strlen(path);
    size_t path_len_no_trailing_slash = path_len;
    while (path[path_len_no_trailing_slash - 1] == '/') {
      path_len_no_trailing_slash = path_len_no_trailing_slash - 1;
    }
    if (path_len > path_len_no_trailing_slash) {
      response.response_code = HTTPSC_MovedPermanently;
      response.header_only = true;
      http_add_header(&response, "Location", sv_from_parts(path, path_len_no_trailing_slash));
      http_send_headers(conn, &response);
      goto end;
    }

    implodeURIComponent(path);

    wpath = path + 1;
  } else {
    wpath = path;
    wpath[0] = '.';
  }
  // TODO: look for index.html

  char* resolved_path = NULL;
  resolved_path = realpath(wpath, NULL);
  if (resolved_path == NULL) {
    if (errno == ENOENT) {
      response.response_code = HTTPSC_NotFound;
      http_send_error(conn, &response);
    } else if (errno == EACCES) {
      response.response_code = HTTPSC_Forbidden;
      http_send_error(conn, &response);
      perror("realpath");
    } else {
      response.response_code = HTTPSC_InternalServerError;
      http_send_error(conn, &response);
      perror("realpath");
    }
    goto end;
  }

  if (strncmp(current_working_directory.data, resolved_path, current_working_directory.count) != 0) {
    // hide files not in working directory
    response.response_code = HTTPSC_NotFound;
    http_send_error(conn, &response);
    goto end;
  }

  char* web_path = resolved_path + current_working_directory.count;

  struct stat statbuf;
  if (stat(resolved_path, &statbuf) == -1) {
    perror("fstat");
    goto end;
  }

  if (S_ISREG(statbuf.st_mode)) {
    http_serve_file(conn, &request, &response, resolved_path, statbuf);
  } else if (S_ISDIR(statbuf.st_mode)) {
    http_serve_directory(conn, &request, &response, resolved_path, web_path);
  }

  if (resolved_path != NULL) {
    free(resolved_path);
  }

 end:
  apache2_log_response(conn, &request, &response);
}
