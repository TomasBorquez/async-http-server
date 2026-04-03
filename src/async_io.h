#pragma once

#include "base.h"
#include "http.h"

typedef enum {
  ACCEPT,
  GENERIC,
} OpType;

typedef struct {
  int res;
  int fd;

  aco_t *co;
  Server *server;
  OpType type;
} CoroCtx;

typedef struct {
  char *data;
  ssize_t file_size;
  bool success;
} FileInfo;

int CoAwait(CoroCtx *ctx, struct io_uring_sqe *sqe);

ssize_t GetFileSize(int fd);

size_t AsyncRecv(CoroCtx *ctx, void *buff, size_t buff_size);
size_t AsyncSend(CoroCtx *ctx, void *buff, size_t buff_size);
size_t AsyncWriteFile(char *file_path, String buff);
FileInfo AsyncReadFile(char *file_path);
