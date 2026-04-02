smatch ./main.c && \
  gcc main.c ./vendor/libaco/aco.c ./vendor/libaco/acosw.S -g3 -Wall -fsanitize=address,undefined -L./vendor/cJSON -lcjson -luring -o ./build/main && \
  ./build/main

# smatch ./main.c && gcc main.c -O3 -L./vendor/cJSON -lcjson -o ./build/main && valgrind ./build/main
