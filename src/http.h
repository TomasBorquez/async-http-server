#pragma once

#include <arpa/inet.h>
#include <liburing.h>

#define ACO_USE_ASAN
#include "aco.h"
#include "base.h"
#include "cJSON.h"

#if !defined(BUFFER_SIZE)
#define BUFFER_SIZE 1024
#endif

#if !defined(PATHS_MAX)
#define PATHS_MAX 200
#endif

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

typedef struct {
  int32_t sockfd;
  struct sockaddr_in host_addr;
  size_t host_addrlen;

  Path paths[PATHS_MAX];
  size_t num_paths;
} Server;

typedef struct {
  aco_t* main_co;
  aco_share_stack_t* shared_stack;
  struct io_uring ring;
} State;
extern State g_state;

Server NewServer(int32_t port);

void ServerGet(Server *server, char *path, Handler handler);
void ServerPost(Server *server, char *path, Handler handler);

Method MethodMatch(char *method);
char *StatusMatch(size_t status);

bool ReadUntil(char **buffer, char *dest, size_t dest_size, char delim);
bool SkipUntil(char **buffer, char delim);

char *GetHeader(HeaderVector *headers, const char *key);
bool SetHeader(HeaderVector *headers, const char *key, const char *value);

void ServerListen(Server *server);

char *JSON(Response *res, size_t status, cJSON *object);

