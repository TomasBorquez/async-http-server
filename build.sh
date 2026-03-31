smatch ./main.c && gcc main.c -g -L./vendor/cJSON -lcjson -o ./build/main && valgrind ./build/main
