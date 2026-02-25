#include "utils.h"
#include <libavutil/log.h>

static int verbose_global = 0;

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
    printf("-xa<n>\tlist files with duplicate audio hash (n = min copies, default 2)\n");
    printf("-xh<n>\tlist files with duplicate file hash (alias: -xf<n>) (n = min copies, default 2)\n");
    printf("Note: -xa/-xh are mutually exclusive and cannot be combined with -f, -a, or -h\n");
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

// FFmpeg logging callback logic
// Needs to access current file path from hashing module
extern char current_processing_file[MAX_PATH_LENGTH];

void custom_log_callback(void *ptr, int level, const char *fmt, va_list vl) {
    if (level > av_log_get_level()) return;
    
    // Only print errors/warnings if not verbose, or everything if verbose
    if (!verbose_global && level > AV_LOG_ERROR) return;

    if (current_processing_file[0] != '\0') {
        fprintf(stderr, "[FFmpeg] File: %s ", current_processing_file);
    } else {
        fprintf(stderr, "[FFmpeg] ");
    }
    vfprintf(stderr, fmt, vl);
}

void init_logging_callback(int verbose) {
    verbose_global = verbose;
    av_log_set_callback(custom_log_callback);
    if (verbose) {
        av_log_set_level(AV_LOG_INFO);
    } else {
        av_log_set_level(AV_LOG_ERROR);
    }
}

void print_duplicates(sqlite3 *db, int type, int min_count) {
    const char *column = (type == DUPE_AUDIO) ? "audio_md5" : "md5";
    char *sql = NULL;

    // Use asprintf to build the query dynamically
    if (asprintf(&sql, 
        "SELECT t1.filepath, t1.%s "
        "FROM files t1 "
        "JOIN ( "
        "    SELECT %s "
        "    FROM files "
        "    WHERE %s IS NOT 'N/A' AND %s IS NOT 'Not calculated' "
        "    GROUP BY %s "
        "    HAVING COUNT(*) >= %d "
        ") t2 ON t1.%s = t2.%s "
        "ORDER BY t1.%s, t1.filepath;", 
        column, column, column, column, column, min_count, column, column, column) == -1) {
        fprintf(stderr, "Memory: Error allocating SQL query\n");
        return;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL: Error preparing duplicate query: %s\n", sqlite3_errmsg(db));
        free(sql);
        return;
    }

    char prev_hash[MD5_DIGEST_LENGTH * 2 + 1] = {0};
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *filepath = (const char *)sqlite3_column_text(stmt, 0);
        const char *hash = (const char *)sqlite3_column_text(stmt, 1);

        if (!hash) continue;

        if (!first && strcmp(hash, prev_hash) != 0) {
            printf("\n"); // Space between groups
        }
        
        // Print just the filename as requested? "output a list of filenames sorted by hash"
        // Actually typically duplicate finders print the full path so you can act on them.
        // User said "list of filenames", but context implies path. I'll print full path.
        printf("%s\n", filepath);
        
        strncpy(prev_hash, hash, sizeof(prev_hash) - 1);
        first = 0;
    }

    sqlite3_finalize(stmt);
    free(sql);
}
