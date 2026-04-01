#define BASE_IMPLEMENTATION
#include "base.h"
#include "vendor/cJSON/cJSON.h"

#include <arpa/inet.h>
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
  char *body;
  size_t body_length;
} Request;

typedef struct {
  size_t status;
  char *response_body;
  HeaderVector headers;
} Response;

typedef char *(*Handler)(Request *req, Response *res);

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
  Assert(result.sockfd != -1, "webserver (socket)");

  int opt = 1;
  Assert(setsockopt(result.sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == 0, "could not set reusable socket");

  LogSuccess("Socket created successfully");

  result.host_addrlen = sizeof(result.host_addr);

  result.host_addr.sin_family = AF_INET;
  result.host_addr.sin_port = htons(port);
  result.host_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  result.client_addrlen = sizeof(result.client_addr);

  Assert(bind(result.sockfd, (struct sockaddr *)&result.host_addr, result.host_addrlen) == 0, "webserver (bind)");
  LogSuccess("Socket successfully bound to address");

  Assert(listen(result.sockfd, SOMAXCONN) == 0, "webserver (listen)");
  LogSuccess("Server listening for connections");
  return result;
}

void ServerGet(Server *server, char *path, Handler handler) {
  Path result = {
    .path = path,
    .handler = handler,
    .method = GET,
  };
  Assert(server->num_paths != PATHS_MAX, "cannot add more than 100 paths");
  server->paths[server->num_paths++] = result;
};

void ServerPost(Server *server, char *path, Handler handler) {
  Path result = {
    .path = path,
    .handler = handler,
    .method = POST,
  };
  Assert(server->num_paths != PATHS_MAX, "cannot add more than 100 paths");
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

char *StatusMatch(size_t status) {
  if (status == 200) {
    return "OK";
  }

  if (status == 201) {
    return "Created";
  }

  if (status == 400) {
    return "Bad Request";
  }

  if (status == 404) {
    return "Not Found";
  }

  return "";
}


bool ReadUntil(char **buffer, char *dest, size_t dest_size, char delim) {
  size_t i = 0;
  while (**buffer != delim && **buffer != '\0') {
    if (i >= dest_size - 1) return false;
    dest[i++] = *(*buffer)++;
  }

  dest[i] = '\0';
  if (**buffer == '\0') return false;
  (*buffer)++;
  return true;
}

bool SkipUntil(char **buffer, char delim) {
  while (**buffer != delim && **buffer != '\0') {
    (*buffer)++;
  }

  if (**buffer == '\0') return false;
  (*buffer)++;
  return true;
}

// POST / HTTP/1.1\r\n
// Host: localhost:8080\r\n
// Accept-Encoding: gzip, deflate\r\n
// Accept: */*\r\n
// Connection: keep-alive\r\n
// User-Agent: HTTPie/3.2.2\r\n
// Content-Length: 18\r\n
// \r\n
// {"test": "random"}
static Request ParseRequest(char *req_buffer, char **body_start) {
  Request result = {0};
  LogInfo("buffer: \n%s", req_buffer);

  char method[7] = {0};
  if (!ReadUntil(&req_buffer, method, sizeof(method), ' ')) return result;

  result.method = MethodMatch(method);
  LogInfo("method: \"%s\"", method);

  if (!ReadUntil(&req_buffer, result.path, sizeof(result.path), ' ')) return result;
  LogInfo("path: \"%s\"", result.path);

  if (!SkipUntil(&req_buffer, '\n')) return result;

  while (*req_buffer != '\0') {
    Header header = {0};
    if (*req_buffer == '\r' && *(req_buffer + 1) == '\n') {
      req_buffer += 2;
      break;
    }

    if (!ReadUntil(&req_buffer, header.key, sizeof(header.key), ':')) return result;
    if (*req_buffer == ' ') req_buffer++;
    if (!ReadUntil(&req_buffer, header.value, sizeof(header.value), '\r')) return result;
    if (!SkipUntil(&req_buffer, '\n')) return result;
    VecPush(result.headers, header)
  }

  *body_start = req_buffer;

  LogSuccess("Processed all headers");
  for (size_t i = 0; i < result.headers.length; i++) {
    Header curr_header = result.headers.data[i];
    LogInfo("%s:%s", curr_header.key, curr_header.value);
  }

  return result;
}

char *GetHeader(Request *req, char *key) {
  for (size_t i = 0; i < req->headers.length; i++) {
    Header curr_header = req->headers.data[i];
    if (strcmp(curr_header.key, key) == 0) {
      return req->headers.data[i].value;
    }
  }
  return "";
}

char *ConstructHeaders(Arena *arena, HeaderVector headers) {
  StringBuilder response = StringBuilderCreate(arena);
  for (size_t i = 0; i < headers.length; i++) {
    Header curr_header = headers.data[i];
    StringBuilderAppend(arena, &response, s(curr_header.key));
    StringBuilderAppend(arena, &response, S(": "));
    StringBuilderAppend(arena, &response, s(curr_header.value));
    StringBuilderAppend(arena, &response, S("\r\n"));
  }
  return response.buffer.data;
}

void ServerListen(Server *server){
  char req_buffer[BUFFER_SIZE];
  char default_response[] = "HTTP/1.0 404 Not Found\r\n"
                            "Server: webserver-c\r\n"
                            "\r\n";

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

    int valread = read(newsockfd, req_buffer, BUFFER_SIZE);
    if (valread < 0) {
      perror("webserver (read)");
      continue;
    }
    req_buffer[valread] = '\0';

    char *body_start = "";
    Request req = ParseRequest(req_buffer, &body_start);
    char *content_length = GetHeader(&req, "Content-Length");
    if (*content_length != '\0') {
      size_t content_size = strtoul(content_length, NULL, 10);
      LogInfo("content_size = %lu", content_size);

      char *body_buffer = malloc(content_size + 1);
      size_t total = strlen(body_start);
      if (total > content_size) total = content_size;

      mempcpy(body_buffer, body_start, total);

      while (total < content_size) {
        ssize_t n = recv(server->sockfd, body_buffer + total, content_size - total, 0);
        if (n < 0) {
          LogError("recv error: %s", strerror(n));
          break;
        }
        if (n == 0) {
          LogError("client closed connection");
          break;
        }
        total += n;
      }
      body_buffer[total] = '\0';
      req.body = body_buffer;
      req.body_length = total;

      LogInfo("body_buffer: %s", body_buffer);
    }

    bool found = false;
    Response res = { .status = 200, .response_body = "" };
    for (size_t i = 0; i < server->num_paths; i++) {
      Path curr_path = server->paths[i];
      if (strcmp(curr_path.path, req.path) == 0 && req.method == curr_path.method) {
        res.response_body = curr_path.handler(&req, &res);
        found = true;
        break;
      }
    }

    if (!found) {
      LogWarn("Could not match path=%s and method=%d", req.path, req.method);
      int valwrite = write(newsockfd, default_response, sizeof(default_response));
      if (valwrite < 0) {
        perror("webserver (write)");
      }
      continue;
    }

    Arena *arena = ArenaCreate(8000);
    String response = F(arena,
                        "HTTP/1.0 %lu %s\r\n"
                        "Server: webserver-c\r\n"
                        "%s"
                        "\r\n"
                        "%s",
                        res.status,
                        StatusMatch(res.status),
                        ConstructHeaders(arena, res.headers),
                        res.response_body);

    LogInfo("FinalResponse: %s", response.data);
    int valwrite = write(newsockfd, response.data, response.length);
    if (valwrite < 0) {
      perror("webserver (write)");
      continue;
    }

    close(newsockfd);
  }
}

typedef struct {
  char *username;
  char *email;
} User;

User users[50] = {
  { .username = "pedro", .email = "pedro@pascal.com" },
  { .username = "tomas", .email = "tomas@pascal.com" },
};
size_t users_len = 2;

char *JSON(Response *res, cJSON *object) {
  Header header = {
    .key = "Content-Type",
    .value = "application/json"
  };

  VecPush(res->headers, header)
  return cJSON_Print(object);
}

char *GetUsers(Request *req, Response *res) {
  cJSON *users_array = cJSON_CreateArray();
  for (size_t i = 0; i < users_len; i++) {
      cJSON *user_object = cJSON_CreateObject();
      cJSON_AddItemToArray(users_array, user_object);
      cJSON_AddStringToObject(user_object, "username", users[i].username);
      cJSON_AddStringToObject(user_object, "email", users[i].email);
  }
  res->status = 200;
  // TODO: FREE
  return JSON(res, users_array);
}

char *CreateUser(Request *req, Response *res) {
  cJSON *users_json = cJSON_Parse(req->body);
  if (users_json == NULL) {
      const char *error_ptr = cJSON_GetErrorPtr();
      if (error_ptr != NULL) {
          LogError("error before: %s", error_ptr);
      }
      res->status = 400;
      return "";
  }

  User user = {0};
  cJSON *username = cJSON_GetObjectItemCaseSensitive(users_json, "username");
  if (!cJSON_IsString(username) || (username->valuestring == NULL)) {
    res->status = 400;
    return "";
  }

  cJSON *email = cJSON_GetObjectItemCaseSensitive(users_json, "email");
  if (!cJSON_IsString(email) || (email->valuestring == NULL)) {
    res->status = 400;
    return "";
  }

  user.username = username->valuestring;
  user.email = email->valuestring;
  users[users_len++] = user;

  res->status = 201;
  return "CREATED YOUR USER";
}

int main(void) {
  Server server = NewServer(8080);

  ServerGet(&server, "/users", &GetUsers);
  ServerPost(&server, "/user", &CreateUser);

  ServerListen(&server);

  return 0;
}
