#ifndef UTILS_H
#define UTILS_H

#include "common.h"
#include <sqlite3.h>

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

void init_logging_callback(int verbose);
void print_duplicates(sqlite3 *db, int type, int min_count);

#define DUPE_AUDIO 1
#define DUPE_FILE 2

#endif
