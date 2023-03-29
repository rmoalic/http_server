
#ifndef HTTP_HEADER
#define HTTP_HEADER

#include <sys/socket.h>
#include <netdb.h>
#include "sv.h"
#include "http_status_code.h"

#define HTTP_HEADER_MAX_LEN 1024
#define HTTP_HEADER_NAME_MAX_LEN 41
#define HTTP_HEADER_SEPARATOR ": "
#define HTTP_ENDL "\r\n"

extern String_View current_working_directory;

enum HTTP_Verb {
  GET,
  HEAD,
  POST,
  PUT,
  DELETE
};

struct HTTP_Request {
  enum HTTP_Verb verb;
  String_View path;
  String_View version;  
  String_View hFrom;
  String_View hHost;
  String_View hUseragent;
  String_View hAccept;
  String_View hConnection;
  String_View hIfModifiedSince;
};

struct HTTP_Response {
  enum HTTPSTATUSCODES response_code;

  bool header_only;
  bool header_sent;
  bool status_line_sent;

  size_t header_len;
  char header[HTTP_HEADER_MAX_LEN];

  size_t response_len;
};

struct HTTP_Connection {
  int client_fd;
  struct sockaddr* client_addr;
  socklen_t client_addr_len;

  char hbuf[NI_MAXHOST];
  char sbuf[NI_MAXSERV];


  bool close_on_first_responce;
};


void http_handle_connection(struct HTTP_Connection*);

#endif
