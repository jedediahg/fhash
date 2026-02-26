#include "utils.h"
#include <libavutil/log.h>

static int verbose_global = 0;

void help() {
    printf("fhash scan [options]\n");
    printf("  -s <startpath>\tdirectory to scan (default .)\n");
    printf("  -e <extlist>\tcomma-separated extensions to include (e.g., mp3,flac)\n");
    printf("  -r\t\trecurse directories\n");
    printf("  -h\t\tcalculate MD5 hash of files\n");
    printf("  -a\t\tcalculate MD5 hash of audio stream\n");
    printf("  -f\t\tforce re-index (update existing rows)\n");
    printf("\n");
    printf("fhash dupe [options] (-xa<n> | -xh<n>)\n");
    printf("fhash link [options] (-xa<n> | -xh<n>) -l{mode}\n");
    printf("  -xa<n>\t\taudio hash duplicates (min group size n, default 2)\n");
    printf("  -xh<n>\t\tfile hash duplicates (min group size n, default 2)\n");
    printf("  -l{mode}\tlink duplicates (s=shallow, d=deep, m=metadata, o=oldest, n=newest)\n");
    printf("  -s/-r/-e\tlimit duplicate queries to path/recursion/extensions (applies to dupe and link)\n");
    printf("\n");
    printf("Global options:\n");
    printf("  -d <dbpath>\tSQLite database path (default ./file_hashes.db)\n");
    printf("  -v\t\tverbose output\n");
    printf("  -dry\t\tdry run; report actions only\n");
    printf("  -help\t\tshow this help\n");
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
        int new_capacity = stack->capacity * 2;
        DirEntry *new_entries = realloc(stack->entries, new_capacity * sizeof(DirEntry));
        if (!new_entries) {
            fprintf(stderr, "Memory allocation error while growing directory stack\n");
            exit(1);
        }
        stack->entries = new_entries;
        stack->capacity = new_capacity;
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

typedef struct {
    char *filepath;
    char hash[MD5_DIGEST_LENGTH * 2 + 1];
    char md5[MD5_DIGEST_LENGTH * 2 + 1];
    char audio_md5[MD5_DIGEST_LENGTH * 2 + 1];
    char filename[MAX_PATH_LENGTH];
    char extension[64];
    int64_t filesize;
    time_t last_check;
    struct stat st;
    int has_stat;
    int depth;
} DupeEntry;

static int path_depth(const char *path) {
    int depth = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/') depth++;
    }
    return depth;
}

static int has_value(const char *s) {
    return s && s[0] != '\0' && strcmp(s, "Not calculated") != 0 && strcmp(s, "N/A") != 0;
}

static int metadata_score(const DupeEntry *e) {
    int score = 0;
    if (has_value(e->md5)) score++;
    if (has_value(e->audio_md5)) score++;
    if (has_value(e->filename)) score++;
    if (has_value(e->extension)) score++;
    if (e->filesize > 0) score++;
    if (e->last_check > 0) score++;
    return score;
}

int ext_matches_filter(const char *extension, char **ext_list, int ext_count) {
    if (ext_count == 0) return 1;
    if (!extension || extension[0] == '\0') return 0;
    for (int i = 0; i < ext_count; i++) {
        if (strcasecmp(extension, ext_list[i]) == 0) return 1;
    }
    return 0;
}

int path_matches_filter(const char *filepath, const char *base, int recurse_dirs) {
    if (!base || base[0] == '\0') return 1;
    size_t base_len = strlen(base);
    if (strncmp(filepath, base, base_len) != 0) return 0;
    const char *rest = filepath + base_len;
    if (*rest == '\0') return 1;
    if (*rest != '/') return 0;
    rest++; // skip '/'
    if (recurse_dirs) return 1;
    return strchr(rest, '/') == NULL;
}

static void free_dupe_entries(DupeEntry *entries, int count) {
    for (int i = 0; i < count; i++) {
        free(entries[i].filepath);
    }
    free(entries);
}

static const DupeEntry* choose_target(DupeEntry *entries, int count, int link_mode) {
    const DupeEntry *target = &entries[0];
    for (int i = 1; i < count; i++) {
        const DupeEntry *candidate = &entries[i];
        switch (link_mode) {
            case LINK_SHALLOW:
                if (candidate->depth < target->depth) target = candidate;
                break;
            case LINK_DEEP:
                if (candidate->depth > target->depth) target = candidate;
                break;
            case LINK_METADATA:
                if (metadata_score(candidate) > metadata_score(target)) target = candidate;
                break;
            case LINK_OLDEST:
                if (candidate->has_stat && !target->has_stat) {
                    target = candidate;
                } else if (candidate->has_stat && target->has_stat && candidate->st.st_mtime < target->st.st_mtime) {
                    target = candidate;
                }
                break;
            case LINK_NEWEST:
                if (candidate->has_stat && !target->has_stat) {
                    target = candidate;
                } else if (candidate->has_stat && target->has_stat && candidate->st.st_mtime > target->st.st_mtime) {
                    target = candidate;
                }
                break;
            default:
                break;
        }
    }
    return target;
}

static void print_group(DupeEntry *entries, int count) {
    for (int i = 0; i < count; i++) {
        printf("%s\n", entries[i].filepath);
    }
}

static void handle_group(sqlite3 *db, DupeEntry *group, int group_size, int link_mode, int dry_run, int type, sqlite3_stmt *ts_stmt, sqlite3_stmt *size_stmt) {
    if (group_size == 0) return;
    if (link_mode == LINK_NONE) {
        print_group(group, group_size);
        printf("\n");
        return;
    }
    const DupeEntry *target = choose_target(group, group_size, link_mode);
    for (int i = 0; i < group_size; i++) {
        DupeEntry *entry = &group[i];
        if (entry == target) {
            printf("[keep] %s\n", entry->filepath);
            continue;
        }
        if (!target->has_stat || !entry->has_stat) {
            fprintf(stderr, "Skipping link for %s (missing stat info)\n", entry->filepath);
            continue;
        }
        if (target->st.st_dev != entry->st.st_dev) {
            fprintf(stderr, "Skipping cross-device link %s -> %s\n", entry->filepath, target->filepath);
            continue;
        }
        if (dry_run) {
            printf("[link] %s -> %s\n", entry->filepath, target->filepath);
            continue;
        }

        char tmp_path[MAX_PATH_LENGTH];
        snprintf(tmp_path, sizeof(tmp_path), "%s.fhash_linkXXXXXX", entry->filepath);
        int tmp_fd = mkstemp(tmp_path);
        if (tmp_fd == -1) {
            fprintf(stderr, "Error creating temp link path for %s: %m\n", entry->filepath);
            continue;
        }
        close(tmp_fd);
        unlink(tmp_path); // free the path for link()

        if (link(target->filepath, tmp_path) != 0) {
            fprintf(stderr, "Error linking %s -> %s: %m\n", tmp_path, target->filepath);
            unlink(tmp_path);
            continue;
        }
        if (rename(tmp_path, entry->filepath) != 0) {
            fprintf(stderr, "Error renaming temp link %s -> %s: %m\n", tmp_path, entry->filepath);
            unlink(tmp_path);
            continue;
        }
        printf("[linked] %s -> %s\n", entry->filepath, target->filepath);

        if (ts_stmt) {
            time_t now = time(NULL);
            char ft[] = {'L', '\0'};
            sqlite3_bind_int64(ts_stmt, 1, now);
            sqlite3_bind_text(ts_stmt, 2, ft, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ts_stmt, 3, entry->filepath, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(ts_stmt) != SQLITE_DONE) {
                fprintf(stderr, "SQL: Error updating timestamp for %s: %s\n", entry->filepath, sqlite3_errmsg(db));
            }
            sqlite3_reset(ts_stmt);
            sqlite3_clear_bindings(ts_stmt);
        }

        if (type == DUPE_AUDIO && size_stmt) {
            int64_t target_size = target->has_stat ? (int64_t)target->st.st_size : target->filesize;
            int64_t entry_size = entry->has_stat ? (int64_t)entry->st.st_size : entry->filesize;
            if (target_size != entry_size) {
                char ft[] = {'L', '\0'};
                sqlite3_bind_int64(size_stmt, 1, target_size);
                sqlite3_bind_text(size_stmt, 2, target->md5, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(size_stmt, 3, time(NULL));
                sqlite3_bind_text(size_stmt, 4, ft, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(size_stmt, 5, entry->filepath, -1, SQLITE_TRANSIENT);
                if (sqlite3_step(size_stmt) != SQLITE_DONE) {
                    fprintf(stderr, "SQL: Error updating size/hash for %s: %s\n", entry->filepath, sqlite3_errmsg(db));
                }
                sqlite3_reset(size_stmt);
                sqlite3_clear_bindings(size_stmt);
            }
        }
    }
    printf("\n");
}

void process_duplicates(sqlite3 *db, int type, int min_count, int link_mode, int dry_run, const char *path_filter, int recurse_filter, char **ext_list, int ext_count) {
    const char *column = (type == DUPE_AUDIO) ? "audio_md5" : "md5";
    char *sql = NULL;
    sqlite3_stmt *ts_stmt = NULL;
    sqlite3_stmt *size_stmt = NULL;

    if (!dry_run && link_mode != LINK_NONE) {
        const char *ts_sql = "UPDATE files SET last_check_timestamp = ?, filetype = ? WHERE filepath = ?;";
        if (sqlite3_prepare_v2(db, ts_sql, -1, &ts_stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQL: Error preparing timestamp update: %s\n", sqlite3_errmsg(db));
            return;
        }
        if (type == DUPE_AUDIO) {
            const char *size_sql = "UPDATE files SET filesize = ?, md5 = ?, last_check_timestamp = ?, filetype = ? WHERE filepath = ?;";
            if (sqlite3_prepare_v2(db, size_sql, -1, &size_stmt, NULL) != SQLITE_OK) {
                fprintf(stderr, "SQL: Error preparing size/hash update: %s\n", sqlite3_errmsg(db));
                sqlite3_finalize(ts_stmt);
                return;
            }
        }
    }

    if (asprintf(&sql, 
        "SELECT filepath, %s, md5, audio_md5, filename, extension, filesize, last_check_timestamp "
        "FROM files "
        "WHERE %s IS NOT 'N/A' AND %s IS NOT 'Not calculated' "
        "AND %s IS NOT 'Bad audio' AND %s IS NOT '0-byte-file' "
        "ORDER BY %s, filepath;", 
        column, column, column, column, column, column) == -1) {
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
    DupeEntry *group = NULL;
    int group_size = 0;
    int group_capacity = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *filepath = (const char *)sqlite3_column_text(stmt, 0);
        const char *hash = (const char *)sqlite3_column_text(stmt, 1);
        if (!hash || !filepath) continue;

        int path_ok = path_matches_filter(filepath, path_filter, recurse_filter);
        const unsigned char *ext_val = sqlite3_column_text(stmt, 5);
        int ext_ok = ext_matches_filter((const char *)ext_val, ext_list, ext_count);

        if (prev_hash[0] != '\0' && strcmp(hash, prev_hash) != 0) {
            if (group_size >= min_count) {
                handle_group(db, group, group_size, link_mode, dry_run, type, ts_stmt, size_stmt);
            }
            free_dupe_entries(group, group_size);
            group = NULL;
            group_size = 0;
            group_capacity = 0;
        }

        if (group_size == group_capacity) {
            int new_cap = group_capacity == 0 ? 8 : group_capacity * 2;
            DupeEntry *tmp = realloc(group, new_cap * sizeof(DupeEntry));
            if (!tmp) {
                fprintf(stderr, "Memory: Error reallocating group\n");
                break;
            }
            group = tmp;
            group_capacity = new_cap;
        }

        if (path_ok && ext_ok) {
            DupeEntry *entry = &group[group_size++];
            memset(entry, 0, sizeof(DupeEntry));
            entry->filepath = strdup(filepath);
            entry->depth = path_depth(filepath);
            strncpy(entry->hash, hash, sizeof(entry->hash) - 1);

            const unsigned char *md5 = sqlite3_column_text(stmt, 2);
            const unsigned char *audio = sqlite3_column_text(stmt, 3);
            const unsigned char *fname = sqlite3_column_text(stmt, 4);
            const unsigned char *ext = sqlite3_column_text(stmt, 5);
            entry->filesize = sqlite3_column_int64(stmt, 6);
            entry->last_check = sqlite3_column_int64(stmt, 7);
            if (md5) strncpy(entry->md5, (const char *)md5, sizeof(entry->md5) - 1);
            if (audio) strncpy(entry->audio_md5, (const char *)audio, sizeof(entry->audio_md5) - 1);
            if (fname) strncpy(entry->filename, (const char *)fname, sizeof(entry->filename) - 1);
            if (ext) strncpy(entry->extension, (const char *)ext, sizeof(entry->extension) - 1);

            if (stat(entry->filepath, &entry->st) == 0) {
                entry->has_stat = 1;
            } else {
                fprintf(stderr, "OS: Error stating %s: %m\n", entry->filepath);
                entry->has_stat = 0;
            }
        }

        strncpy(prev_hash, hash, sizeof(prev_hash) - 1);
    }

    if (group_size >= min_count) {
        handle_group(db, group, group_size, link_mode, dry_run, type, ts_stmt, size_stmt);
    }
    if (group_size > 0) {
        free_dupe_entries(group, group_size);
    }

    sqlite3_finalize(stmt);
    free(sql);

    if (ts_stmt) sqlite3_finalize(ts_stmt);
    if (size_stmt) sqlite3_finalize(size_stmt);
}
