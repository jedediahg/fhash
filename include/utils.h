#ifndef UTILS_H
#define UTILS_H

#include "common.h"

typedef struct {
    char path[MAX_PATH_LENGTH];
} DirEntry;

typedef struct {
    DirEntry *entries;
    int capacity;
    int size;
} DirStack;

void help();
DirStack* create_dir_stack(int capacity);
void push_dir(DirStack *stack, const char *path);
char* pop_dir(DirStack *stack);
void destroy_dir_stack(DirStack *stack);

#endif
