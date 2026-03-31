#define BASE_IMPLEMENTATION
#include "base.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

typedef enum { GET, PUT, POST, PATCH, DELETE } Method;

// TODO: Make strings heap allocated
typedef struct {
  char key[100];
  char value[100];
} Header;
VEC_TYPE(HeaderVector, Header);

typedef struct {
  Method method;
  char path[2000];
  HeaderVector headers;
} Request;

typedef void (*Handler)(Request *request);

typedef struct {
  char *path;
  Method method;
  Handler handler;
} Path;

#define PATHS_MAX 100

typedef struct {
  int32_t sockfd;
  struct sockaddr_in host_addr;
  size_t host_addrlen;
  struct sockaddr_in client_addr;
  size_t client_addrlen;

  Path paths[PATHS_MAX];
  size_t num_paths;
} Server;


Server NewServer(int32_t port) {
  Server result = {0};

  result.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  assert(result.sockfd != -1 && "webserver (socket)");

  int opt = 1;
  assert(setsockopt(result.sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == 0 && "could not set reusable socket");

  LogSuccess("Socket created successfully");

  result.host_addrlen = sizeof(result.host_addr);

  result.host_addr.sin_family = AF_INET;
  result.host_addr.sin_port = htons(port);
  result.host_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  result.client_addrlen = sizeof(result.client_addr);

  assert(bind(result.sockfd, (struct sockaddr *)&result.host_addr, result.host_addrlen) == 0 && "webserver (bind)");
  LogSuccess("Socket successfully bound to address");

  assert(listen(result.sockfd, SOMAXCONN) == 0 && "webserver (listen)");
  LogSuccess("Server listening for connections");
  return result;
}

void ServerGet(Server *server, char *path, Handler handler) {
  Path result = {
    .path = path,
    .handler = handler,
    .method = GET,
  };
  assert(server->num_paths != PATHS_MAX && "cannot add more than 100 paths");
  server->paths[server->num_paths++] = result;
};

Method MethodMatch(char *method) {
  if (strcmp(method, "GET") == 0) {
    return GET;
  }

  if (strcmp(method, "POST") == 0) {
    return POST;
  }

  if (strcmp(method, "DELETE") == 0) {
    return DELETE;
  }

  if (strcmp(method, "PUT") == 0) {
    return PUT;
  }

  if (strcmp(method, "PATCH") == 0) {
    return PATCH;
  }

  return -1;
}

// GET / HTTP/1.1\r\n
// Host: localhost:8080\r\n
// Accept-Encoding: gzip, deflate\r\n
// Accept: */*\r\n
// Connection: keep-alive\r\n
// User-Agent: HTTPie/3.2.2\r\n
// \r\n

// TODO: add both length checks and \0 checks
static Request ParseRequest(char *buffer) {
  Request result = {0};
  LogInfo("buffer: \n%s", buffer);

  char method[7] = "";
  size_t i = 0;
  while (*buffer != ' ') {
    method[i] = *buffer++;
    i++;
  }

  result.method = MethodMatch(method);
  LogInfo("method: \"%s\"", method);

  i = 0;
  buffer++;
  while (*buffer != ' ') {
    result.path[i] = *buffer++;
    i++;
  }
  LogInfo("path: \"%s\"", result.path);

  // skip first line
  while(*buffer != '\n') {
    buffer++;
  }

  buffer++;
  while (*buffer != '\0') {
    i = 0;
    Header header = {0};
    while (*buffer != ':' && *buffer != '\0') {
      header.key[i] = *buffer++;
      i++;
    }
    if (*buffer == '\0') {
      break;
    }
    buffer++;
    buffer++;

    i = 0;
    while (*buffer != '\r' && *buffer != '\0') {
      header.value[i] = *buffer++;
      i++;
    }
    if (*buffer == '\0') {
      break;
    }
    buffer++;
    buffer++;
    VecPush(result.headers, header)
  }

  LogSuccess("Processed all headers");
  for (size_t i = 0; i < result.headers.length; i++) {
    Header curr_header = result.headers.data[i];
    LogInfo("key: %s, val: %s", curr_header.key, curr_header.value);
  }

  return result;
}

void ServerListen(Server *server){
  char buffer[BUFFER_SIZE];
  char resp[] = "HTTP/1.0 200 OK\r\n"
                "Server: webserver-c\r\n"
                "Content-type: text/html\r\n"
                "\r\n"
                "<html>hello, world</html>\r\n";

  while (true) {
    int newsockfd = accept(server->sockfd, (struct sockaddr *)&server->host_addr, (socklen_t *)&server->host_addrlen);
    if (newsockfd < 0) {
      perror("webserver (accept)");
      continue;
    }
    LogSuccess("Connection accepted");

    int sockn = getsockname(newsockfd, (struct sockaddr *)&server->client_addr, (socklen_t *)&server->client_addrlen);
    if (sockn < 0) {
      perror("webserver (getsockname)");
      continue;
    }

    int valread = read(newsockfd, buffer, BUFFER_SIZE);
    if (valread < 0) {
      perror("webserver (read)");
      continue;
    }

    bool found = false;
    Request request = ParseRequest(buffer);
    for (size_t i = 0; i < server->num_paths; i++) {
      Path curr_path = server->paths[i];
      if (strcmp(curr_path.path, request.path) == 0 && request.method == curr_path.method) {
        curr_path.handler(&request);
        found = true;
        break;
      }
    }

    if (!found) {
      LogWarn("Could not match path=%s and method=%d", request.path, request.method);
    }

    for (size_t i = 0; i < request.headers.length; i++) {
      Header curr_header = request.headers.data[i];
      if (strcmp(curr_header.key, "Content-Length") == 0) {
        char *endptr;
        size_t content_length = strtol(curr_header.value, &endptr, 10);
        LogInfo("content_length = %lu", content_length);

        char *body_buffer = malloc((content_length + 1) * sizeof(char));
        recv(server->sockfd, buffer, sizeof(buffer), 0);
        LogInfo("body_buffer: %s", buffer);
      }
    }

    int valwrite = write(newsockfd, resp, strlen(resp));
    if (valwrite < 0) {
      perror("webserver (write)");
      continue;
    }

    close(newsockfd);
  }
}

void HomeHandler(Request *request) {
  LogInfo("Hello from home");
}

int main(void) {
  Server server = NewServer(8080);

  ServerGet(&server, "/", &HomeHandler);

  ServerListen(&server);

  return 0;
}
