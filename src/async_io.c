#include "async_io.h"

#include <sys/stat.h>
#include <liburing.h>
#include <sys/ioctl.h>

int CoAwait(CoroCtx *ctx, struct io_uring_sqe *sqe ) {
  ctx->type = GENERIC;
  io_uring_sqe_set_data(sqe, ctx);
  io_uring_submit(&g_state.ring);
  aco_yield();
  return ctx->res;
}

size_t AsyncRecv(CoroCtx *ctx, void *buff, size_t buff_size) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(&g_state.ring);
  io_uring_prep_recv(sqe, ctx->fd, buff, buff_size, 0);
  return CoAwait(ctx, sqe);
}

size_t AsyncSend(CoroCtx *ctx, void *buff, size_t buff_size) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(&g_state.ring);
  io_uring_prep_send(sqe, ctx->fd, buff, buff_size, 0);
  return CoAwait(ctx, sqe);
}

ssize_t GetFileSize(int fd) {
  struct stat st;

  if (fstat(fd, &st) < 0) {
    perror("fstat");
    return -1;
  }

  if (S_ISBLK(st.st_mode)) {
    ssize_t bytes;
    if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
      perror("ioctl");
      return -1;
    }

    return bytes;
  }

  if (S_ISREG(st.st_mode)) {
    return st.st_size;
  }

  return -1;
}

FileInfo AsyncReadFile(char *file_path) {
  CoroCtx *ctx = aco_get_arg();
  int file_fd = open(file_path, O_RDONLY);
  if (file_fd < 0) {
    perror("AsyncReadFile");
    return (FileInfo) { .data = "", .file_size = 0, .success = false };
  }

  off_t file_size = GetFileSize(file_fd);
  char *buff = malloc(file_size + 1);
  buff[file_size] = '\0';

  struct io_uring_sqe *sqe = io_uring_get_sqe(&g_state.ring);
  io_uring_prep_read(sqe, file_fd, buff, file_size, 0);
  ctx->type = GENERIC;
  io_uring_sqe_set_data(sqe, ctx);
  io_uring_submit(&g_state.ring);
  aco_yield();
  return (FileInfo) { .data = buff, .file_size = file_size };
}

size_t AsyncWriteFile(char *file_path, String buff) {
  CoroCtx *ctx = aco_get_arg();
  int file_fd = open(file_path, O_WRONLY);
  if (file_fd < 0) {
    perror("AsyncWriteFile");
    return -1;
  }

  struct io_uring_sqe *sqe = io_uring_get_sqe(&g_state.ring);
  io_uring_prep_write(sqe, file_fd, buff.data, buff.length, 0);
  return CoAwait(ctx, sqe);
}
