#include "http.h"
#include "async_io.h"

State g_state = {0};

Server NewServer(int32_t port) {
  Server result = {0};

  result.sockfd = socket(AF_INET, SOCK_STREAM, 0);
  Assert(result.sockfd != -1, "could not create socket");

  int opt = 1;
  Assert(setsockopt(result.sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == 0, "could not set reusable socket");
  LogSuccess("Socket created successfully");

  result.host_addrlen = sizeof(result.host_addr);
  result.host_addr.sin_family = AF_INET;
  result.host_addr.sin_port = htons(port);
  result.host_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  Assert(bind(result.sockfd, (struct sockaddr *)&result.host_addr, result.host_addrlen) == 0, "could not bind socket");
  LogSuccess("Socket successfully bound to address");

  Assert(listen(result.sockfd, SOMAXCONN) == 0, "could not listen to socket");
  LogSuccess("Socket listening for connections");
  return result;
}

void ServerGet(Server *server, char *path, Handler handler) {
  Path result = {
      .path = path,
      .handler = handler,
      .method = GET,
  };
  Assert(server->num_paths != PATHS_MAX, "cannot add more than %d paths", PATHS_MAX);
  server->paths[server->num_paths++] = result;
};

void ServerPost(Server *server, char *path, Handler handler) {
  Path result = {
      .path = path,
      .handler = handler,
      .method = POST,
  };
  Assert(server->num_paths != PATHS_MAX, "cannot add more than %d paths", PATHS_MAX);
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

char *MatchMethod(Method method) {
  if (method == GET) {
    return "GET";
  }

  if (method == POST) {
    return "POST";
  }

  if (method == DELETE) {
    return "DELETE";
  }

  if (method == PUT) {
    return "PUT";
  }

  if (method == PATCH) {
    return "PATCH";
  }

  return "";
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

static Request parse_request(char *req_buffer, char **body_start) {
  Request result = {0};

  char method[7] = {0};
  if (!ReadUntil(&req_buffer, method, sizeof(method), ' ')) return result;
  result.method = MethodMatch(method);

  if (!ReadUntil(&req_buffer, result.path, sizeof(result.path), ' ')) return result;
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
    VecPush(result.headers, header);
  }

  *body_start = req_buffer;
  return result;
}

char *GetHeader(HeaderVector *headers, const char *key) {
  for (size_t i = 0; i < headers->length; i++) {
    if (strcmp(headers->data[i].key, key) == 0) {
      return headers->data[i].value;
    }
  }
  return NULL;
}

bool SetHeader(HeaderVector *headers, const char *key, const char *value) {
  if (GetHeader(headers, key) != NULL) {
    return false;
  }

  Header header = {0};
  strncpy(header.key, key, sizeof(header.key) - 1);
  strncpy(header.value, value, sizeof(header.value) - 1);

  VecPush(*headers, header);
  return true;
}

static char *construct_headers(Arena *arena, HeaderVector headers) {
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

static const char default_response[] = "HTTP/1.0 404 Not Found\r\n"
                                       "Server: webserver-c\r\n"
                                       "\r\n";
static void handle_client(void) {
  char *req_buffer = malloc(BUFFER_SIZE);
  Arena *arena = ArenaCreate(1000);

  CoroCtx *ctx = aco_get_arg();

  ssize_t recv_size = AsyncRecv(ctx, req_buffer, BUFFER_SIZE - 1);
  if (recv_size <= 0) {
    perror("(AsyncRecv 1)");
    goto cleanup;
  }
  req_buffer[recv_size] = '\0';

  char *body_start = "";
  Request req = parse_request(req_buffer, &body_start);
  req.arena = arena;
  req.body_start = body_start;

  bool found = false;
  Response res = {.status = 200, .response_body = ""};
  for (size_t i = 0; i < ctx->server->num_paths; i++) {
    Path curr_path = ctx->server->paths[i];
    if (strcmp(curr_path.path, req.path) == 0 && req.method == curr_path.method) {
      // TODO: Remove and add no return
      res.response_body = curr_path.handler(&req, &res);
      found = true;
      break;
    }
  }

  if (!found) {
    size_t send_size = AsyncSend(ctx, (char *)default_response, sizeof(default_response));
    if (send_size < 0) {
      perror("(AsyncSend 1)");
    }
    goto cleanup;
  }

  char status_string[4];
  snprintf(status_string, 4, "%zu", res.status);

  StringBuilder builder = StringBuilderCreate(arena);
  StringBuilderAppend(arena, &builder, S("HTTP/1.0 "));
  StringBuilderAppend(arena, &builder, s(status_string));
  StringBuilderAppend(arena, &builder, S(" "));
  StringBuilderAppend(arena, &builder, s(StatusMatch(res.status)));
  StringBuilderAppend(arena, &builder, S("\r\n"));
  StringBuilderAppend(arena, &builder, S("Server: webserver-c\r\n"));
  StringBuilderAppend(arena, &builder, s(construct_headers(arena, res.headers)));
  StringBuilderAppend(arena, &builder, S("\r\n"));
  StringBuilderAppend(arena, &builder, s(res.response_body));

  int valwrite = AsyncSend(ctx, builder.buffer.data, builder.buffer.length);
  if (valwrite < 0) {
    perror("(AsyncSend 2)");
    goto cleanup;
  }

cleanup:
  LogInfo("[%lu] %s %s", res.status, MatchMethod(req.method), req.path);
  close(ctx->fd);
  ArenaFree(arena);
  free(req_buffer);
  aco_exit();
}

Body AsyncReadBody(Request *req) {
  char *content_length = GetHeader(&req->headers, "Content-Length");
  Body result = {0};
  if (content_length == NULL) {
    return result;
  }

  if (req->body_start == NULL) {
    return result;
  }

  size_t content_size = strtoul(content_length, NULL, 10);
  char *body_buffer = ArenaAllocChars(req->arena, content_size + 1);
  size_t total = strlen(req->body_start);
  if (total > content_size) total = content_size;

  mempcpy(body_buffer, req->body_start, total);

  CoroCtx *ctx = aco_get_arg();
  while (total < content_size) {
    ssize_t n = AsyncRecv(ctx, body_buffer + total, content_size - total);
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
  result.data = body_buffer;
  result.len = total;
  result.success = true;

  return result;
}

static CoroCtx *add_accept(Server *server) {
  CoroCtx *ctx = calloc(1, sizeof(CoroCtx));
  ctx->type = ACCEPT;
  ctx->server = server;

  struct io_uring_sqe *sqe = io_uring_get_sqe(&g_state.ring);
  Assert(sqe != NULL, "SQ ring full — add backpressure");
  io_uring_prep_accept(sqe, server->sockfd, (struct sockaddr *)&server->host_addr, (socklen_t *)&server->host_addrlen, 0);
  io_uring_sqe_set_data(sqe, ctx);
  io_uring_submit(&g_state.ring);
  return ctx;
}

void ServerListen(Server *server) {
  aco_thread_init(NULL);

  g_state.main_co = aco_create(NULL, NULL, 0, NULL, NULL);
  g_state.shared_stack = aco_share_stack_new(0);
  struct io_uring_params params = {
      .flags = IORING_SETUP_SQPOLL,
      .sq_thread_idle = 2000,
      .cq_entries = 65536,
  };
  io_uring_queue_init_params(4096, &g_state.ring, &params);

  add_accept(server);

  struct io_uring_cqe *cqe;
  while (true) {
    if (io_uring_wait_cqe(&g_state.ring, &cqe)) {
      perror("(io_uring_wait_cqe)");
      continue;
    }

    CoroCtx *ctx = io_uring_cqe_get_data(cqe);
    int res = cqe->res;
    io_uring_cqe_seen(&g_state.ring, cqe);

    if (res < 0) {
      LogError("On coroutine type %d, got error: %s", ctx->type, strerror(-res));
    }

    if (ctx->type == ACCEPT) {
      if (res >= 0) {
        ctx->fd = res;
        ctx->co = aco_create(g_state.main_co, g_state.shared_stack, 0, handle_client, ctx);

        aco_resume(ctx->co);
        if (ctx->co->is_end == true) {
          aco_destroy(ctx->co);
          free(ctx);
        }
      } else {
        aco_destroy(ctx->co);
        free(ctx);
      }

      add_accept(server);
      continue;
    }
    if (ctx->type == GENERIC) {
      ctx->res = res;
      aco_resume(ctx->co);

      if (ctx->co->is_end == true) {
        aco_destroy(ctx->co);
        free(ctx);
      }
      continue;
    }
  }
}

char *JSON(Response *res, size_t status, cJSON *object) {
  SetHeader(&res->headers, "Content-Type", "application/json");
  res->status = status;

  char *result = cJSON_Print(object);
  cJSON_Delete(object);
  return result;
}
