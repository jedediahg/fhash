#include "utils.h"

void help() {
    printf("-help\tshow this help\n");
    printf("-v\tverbose output, default OFF\n");
    printf("-f\tforce re-indexing of files, default OFF, without -h this only updates size and timestamp\n");
    printf("-h\tcalculate MD5 hash of files, default OFF\n");
    printf("-a\tcalculate MD5 hash of audio stream only, default OFF\n");
    printf("-r\recurse directories, default OFF\n");
    printf("-d\tuse the database at <dbpath> location, must include the db filename. Default: ./file_hashes.db\n");
    printf("-s\tstart in <startpath>, this must be a directory\n");
    printf("-e\tindex files with <extensionlist> extensions, default none, use csv format, e.g. jpeg,jpg,flac,mp3,doc\n");
    printf("\n");
}

DirStack* create_dir_stack(int capacity) {
    DirStack *stack = (DirStack*)malloc(sizeof(DirStack));
    if (!stack) {
        fprintf(stderr, "Memory allocation error\n");
        exit(1);
    }
    stack->entries = (DirEntry*)malloc(capacity * sizeof(DirEntry));
    if (!stack->entries) {
        fprintf(stderr, "Memory allocation error\n");
        free(stack);
        exit(1);
    }
    stack->capacity = capacity;
    stack->size = 0;
    return stack;
}

void push_dir(DirStack *stack, const char *path) {
    if (stack->size >= stack->capacity) {
        fprintf(stderr, "Stack overflow\n");
        exit(1);
    }
    strncpy(stack->entries[stack->size].path, path, MAX_PATH_LENGTH - 1);
    stack->entries[stack->size].path[MAX_PATH_LENGTH - 1] = '\0';
    stack->size++;
}

char* pop_dir(DirStack *stack) {
    if (stack->size <= 0) {
        fprintf(stderr, "Stack underflow\n");
        exit(1);
    }
    stack->size--;
    return stack->entries[stack->size].path;
}

void destroy_dir_stack(DirStack *stack) {
    free(stack->entries);
    free(stack);
}
