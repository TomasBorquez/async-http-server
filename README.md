# About
This is my attempt at creating an Async HTTP Server in C, it's only planned to work on `linux_x86`
and only on more "modern" kernel versions (5.6+), here is a simple example on how to use it along with
cJSON:

```c
char *GetUsers(Request *req, Response *res) {
  cJSON *users_array = cJSON_CreateArray();
  for (size_t i = 0; i < users_len; i++) {
      cJSON *user_object = cJSON_CreateObject();
      cJSON_AddItemToArray(users_array, user_object);
      cJSON_AddStringToObject(user_object, "username", users[i].username);
      cJSON_AddStringToObject(user_object, "email", users[i].email);
  }

  cJSON_Delete(users_array);
  return JSON(res, 200, users_array);
}

int main(void) {
  Server server = NewServer(8080);

  ServerGet(&server, "/users", &GetUsers);

  ServerListen(&server);

  return 0;
}
```

## TODOS
- [x] Parse the HTTP request
- [x] Respond to the HTTP request with some basic text
- [x] Basic routing
- [x] Read the headers
- [x] Parse the HTTP
    - [x] Status
    - [x] Method
    - [x] Headers
    - [x] Body
- [x] Abstract parsing
- [x] Abstract getting header key
- [x] HTTP Response creation
    - [x] string/html
    - [x] JSON - with some lib
- [x] io_uring
    - [x] Event loop
    - [x] Async socket handling
    - [x] Add coroutines
    - [x] Async file reading, and file writing
    - [x] Async `optional` body parsing
- [ ] Optimize io_uring, bulk complete tasks and mark as seen
- [ ] Proper error handling
    - [ ] Error logging, perror, strerror and assertions
    - [ ] Assert on invalid library user errors
- [ ] Paths should just be a vector
- [ ] Cleanup API and add missing basic stuff
- [ ] Properly free all resources
- [ ] Find errors with fuzzing
- [ ] Stress Test

## Future
- [ ] Graph based routing

## Resources
- https://bruinsslot.jp/post/simple-http-webserver-in-c/
- https://github.com/DaveGamble/cJSON
- https://libaco.org/docs
- https://www.man7.org/linux/man-pages/man7/io_uring.7.html
- https://unixism.net/loti/tutorial/webserver_liburing.html
