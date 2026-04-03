#define BASE_IMPLEMENTATION
#include "base.h"
#include "cJSON.h"

#include "http.h"
#include "async_io.h"

typedef struct {
  char *username;
  char *email;
} User;

User users[50] = {
  { .username = "pedro", .email = "pedro@pascal.com" },
  { .username = "tomas", .email = "tomas@pascal.com" },
};
size_t users_len = 2;

static char *get_users(Request *req, Response *res) {
  cJSON *users_array = cJSON_CreateArray();
  for (size_t i = 0; i < users_len; i++) {
      cJSON *user_object = cJSON_CreateObject();
      cJSON_AddItemToArray(users_array, user_object);
      cJSON_AddStringToObject(user_object, "username", users[i].username);
      cJSON_AddStringToObject(user_object, "email", users[i].email);
  }

  size_t write_result = AsyncWriteFile("./test_file.md", S("this is a test"));
  if (write_result >= 0) {
    FileInfo file_info = AsyncReadFile("./test_file.md");
    LogInfo("file_info: %s", file_info.data);
  }

  return JSON(res, 200, users_array);
}

static char *create_user(Request *req, Response *res) {
  char *response = "";
  cJSON *users_json = cJSON_Parse(req->body);
  if (users_json == NULL) {
      const char *error_ptr = cJSON_GetErrorPtr();
      if (error_ptr != NULL) {
          LogError("error before: %s", error_ptr);
      }
      res->status = 400;
      response = "INVALID JSON";
      goto cleanup;
  }

  User user = {0};
  cJSON *username = cJSON_GetObjectItemCaseSensitive(users_json, "username");
  if (!cJSON_IsString(username) || (username->valuestring == NULL)) {
    res->status = 400;
    response = "INVALID JSON";
    goto cleanup_username;
  }

  cJSON *email = cJSON_GetObjectItemCaseSensitive(users_json, "email");
  if (!cJSON_IsString(email) || (email->valuestring == NULL)) {
    res->status = 400;
    response = "INVALID JSON";
    goto cleanup_email;
  }

  user.username = username->valuestring;
  user.email = email->valuestring;
  users[users_len++] = user;
  res->status = 201;

cleanup_email:
  cJSON_Delete(email);
cleanup_username:
  cJSON_Delete(username);
cleanup:
  cJSON_Delete(users_json);
  return response;
}

int main(void) {
  Server server = NewServer(8080);

  ServerGet(&server, "/users", &get_users);
  ServerPost(&server, "/user", &create_user);

  ServerListen(&server);

  return 0;
}
