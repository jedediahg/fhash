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
void process_duplicates(sqlite3 *db, int type, int min_count, int link_mode, int dry_run);

#define DUPE_AUDIO 1
#define DUPE_FILE 2
#define LINK_NONE 0
#define LINK_SHALLOW 1
#define LINK_DEEP 2
#define LINK_METADATA 3
#define LINK_OLDEST 4
#define LINK_NEWEST 5

#endif
