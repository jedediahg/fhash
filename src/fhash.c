#include "common.h"
#include "utils.h"
#include "hashing.h"
#include "db.h"
#include "fhash.h"
#include <sqlite3.h>
#include <libavutil/log.h>
#include <ctype.h>

const char *FHASH_VERSION = "1.0";
const char *DB_VERSION = "1.0";

static int ext_cmp(const void *a, const void *b) {
    const char *ea = *(const char *const *)a;
    const char *eb = *(const char *const *)b;
    return strcmp(ea, eb);
}

static void normalize_extension(char *ext) {
    for (char *p = ext; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

static int parse_extensions(const char *extensions_concatenated, char ***ext_list, int *ext_count) {
    *ext_list = NULL;
    *ext_count = 0;

    if (!extensions_concatenated || extensions_concatenated[0] == '\0') {
        return 0;
    }

    char *ext_copy = strdup(extensions_concatenated);
    if (!ext_copy) {
        fprintf(stderr, "Memory: Error allocating extension list copy\n");
        return 1;
    }

    char *token = strtok(ext_copy, ",");
    while (token) {
        while (*token == ' ' || *token == '\t') token++;

        if (*token != '\0') {
            char **tmp = realloc(*ext_list, sizeof(char *) * (size_t)(*ext_count + 1));
            if (!tmp) {
                fprintf(stderr, "Memory: Error reallocating extension list\n");
                for (int i = 0; i < *ext_count; i++) free((*ext_list)[i]);
                free(*ext_list);
                *ext_list = NULL;
                *ext_count = 0;
                free(ext_copy);
                return 1;
            }
            *ext_list = tmp;

            (*ext_list)[*ext_count] = strdup(token);
            if (!(*ext_list)[*ext_count]) {
                fprintf(stderr, "Memory: Error allocating extension token\n");
                for (int i = 0; i < *ext_count; i++) free((*ext_list)[i]);
                free(*ext_list);
                *ext_list = NULL;
                *ext_count = 0;
                free(ext_copy);
                return 1;
            }
            normalize_extension((*ext_list)[*ext_count]);
            (*ext_count)++;
        }

        token = strtok(NULL, ",");
    }

    free(ext_copy);

    if (*ext_count > 1) {
        qsort(*ext_list, (size_t)*ext_count, sizeof(char *), ext_cmp);
    }

    return 0;
}

static void free_extensions(char **ext_list, int ext_count) {
    for (int i = 0; i < ext_count; i++) {
        free(ext_list[i]);
    }
    free(ext_list);
}

static int extension_allowed(const char *filename, char **ext_list, int ext_count, char *extension_out, size_t extension_out_len) {
    extension_out[0] = '\0';

    const char *dot = strrchr(filename, '.');
    if (dot && dot[1] != '\0') {
        snprintf(extension_out, extension_out_len, "%s", dot + 1);
        normalize_extension(extension_out);
    }

    if (ext_count == 0) return 1;
    if (extension_out[0] == '\0') return 0;

    return bsearch(&extension_out, ext_list, (size_t)ext_count, sizeof(char *), ext_cmp) != NULL;
}

int process_file(const char *file_path, sqlite3 *db, sqlite3_stmt *upsert_stmt, sqlite3_stmt *lookup_stmt, int *file_count, int verbose, int hash_file, int hash_audio, int force_rescan, char filetype, const struct stat *st, const char *filename, const char *extension) {
    int64_t filesize = (int64_t)st->st_size;
    int64_t modified_timestamp = (int64_t)st->st_mtime;
    time_t current_time = time(NULL);

    if (!force_rescan) {
        sqlite3_bind_text(lookup_stmt, 1, file_path, -1, SQLITE_TRANSIENT);
        int lookup_rc = sqlite3_step(lookup_stmt);
        if (lookup_rc == SQLITE_ROW) {
            int64_t db_size = sqlite3_column_int64(lookup_stmt, 0);
            int64_t db_mtime = sqlite3_column_int64(lookup_stmt, 1);
            const unsigned char *db_type = sqlite3_column_text(lookup_stmt, 2);
            const unsigned char *db_md5 = sqlite3_column_text(lookup_stmt, 3);
            const unsigned char *db_audio_md5 = sqlite3_column_text(lookup_stmt, 4);
            int file_hash_ready = !hash_file || (db_md5 && strcmp((const char *)db_md5, "Not calculated") != 0);
            int audio_hash_ready = !hash_audio || (db_audio_md5 && strcmp((const char *)db_audio_md5, "Not calculated") != 0);

            if (db_size == filesize &&
                db_mtime == modified_timestamp &&
                db_type && db_type[0] == (unsigned char)filetype &&
                file_hash_ready &&
                audio_hash_ready) {
                sqlite3_reset(lookup_stmt);
                sqlite3_clear_bindings(lookup_stmt);
                return 0;
            }
        } else if (lookup_rc != SQLITE_DONE) {
            fprintf(stderr, "SQL: Error during metadata lookup for %s: %s\n", file_path, sqlite3_errmsg(db));
            sqlite3_reset(lookup_stmt);
            sqlite3_clear_bindings(lookup_stmt);
            return 1;
        }

        sqlite3_reset(lookup_stmt);
        sqlite3_clear_bindings(lookup_stmt);
    }

    char md5_string[MD5_DIGEST_LENGTH * 2 + 1] = {0};
    char audio_md5_string[MD5_DIGEST_LENGTH * 2 + 1] = {0};
    snprintf(md5_string, sizeof(md5_string), "Not calculated");
    snprintf(audio_md5_string, sizeof(audio_md5_string), "Not calculated");

    if (filesize == 0) {
        if (hash_file) strncpy(md5_string, "0-byte-file", sizeof(md5_string) - 1);
        if (hash_audio) strncpy(audio_md5_string, "0-byte-file", sizeof(audio_md5_string) - 1);
    } else {
        if (hash_file) {
            unsigned char md5_hash[MD5_DIGEST_LENGTH];
            if (calculate_md5(file_path, md5_hash) != 0) {
                fprintf(stderr, "Error calculating MD5 hash for file: %s\n", file_path);
                return 1;
            }
            for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                snprintf(&md5_string[i * 2], 3, "%02x", (unsigned int)md5_hash[i]);
            }
        }

        if (hash_audio) {
            unsigned char raw_hash[MD5_DIGEST_LENGTH] = {0};
            if (calculate_audio_md5(file_path, raw_hash) != 0) {
                snprintf(audio_md5_string, sizeof(audio_md5_string), "Bad audio");
            } else {
                for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                    snprintf(&audio_md5_string[i * 2], 3, "%02x", (unsigned int)raw_hash[i]);
                }
            }
        }
    }

    if (verbose) {
        printf("\tMD5: %s\n", md5_string);
        printf("\tAudio MD5: %s\n", audio_md5_string);
        printf("\tFilepath: %s\n", file_path);
        printf("\tFilename: %s\n", filename);
        printf("\tExtension: %s\n", extension);
        printf("\tFilesize: %ld\n", (long)filesize);
        printf("\tTimestamp: %ld\n", (long)current_time);
    }

    char ft_str[2] = {filetype, '\0'};
    sqlite3_bind_text(upsert_stmt, 1, md5_string, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(upsert_stmt, 2, audio_md5_string, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(upsert_stmt, 3, file_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(upsert_stmt, 4, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(upsert_stmt, 5, extension, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(upsert_stmt, 6, filesize);
    sqlite3_bind_int64(upsert_stmt, 7, current_time);
    sqlite3_bind_int64(upsert_stmt, 8, modified_timestamp);
    sqlite3_bind_text(upsert_stmt, 9, ft_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(upsert_stmt, 10, hash_file);
    sqlite3_bind_int(upsert_stmt, 11, hash_audio);

    if (sqlite3_step(upsert_stmt) != SQLITE_DONE) {
        fprintf(stderr, "SQL: Error executing statement for %s: %s\n", file_path, sqlite3_errmsg(db));
        sqlite3_reset(upsert_stmt);
        sqlite3_clear_bindings(upsert_stmt);
        return 1;
    }

    sqlite3_reset(upsert_stmt);
    sqlite3_clear_bindings(upsert_stmt);

    (*file_count)++;
    if (verbose) {
        printf("Processed file: %s\n", file_path);
    }
    return 0;
}

int process_directory(const char *dir_path, sqlite3 *db, sqlite3_stmt *upsert_stmt, sqlite3_stmt *lookup_stmt, int *file_count, int verbose, const char *extensions_concatenated, int force_rescan, int *batch_count, int hash_files, int hash_audio, int recurse_dirs) {
    DirStack *stack = create_dir_stack(STACK_SIZE);
    push_dir(stack, dir_path);

    char **ext_list = NULL;
    int ext_count = 0;
    if (parse_extensions(extensions_concatenated, &ext_list, &ext_count) != 0) {
        destroy_dir_stack(stack);
        return 1;
    }

    while (stack->size > 0) {
        char current_path[MAX_PATH_LENGTH];
        strncpy(current_path, pop_dir(stack), MAX_PATH_LENGTH - 1);
        current_path[MAX_PATH_LENGTH - 1] = '\0';

        if (verbose) {
            printf("Current Path: %s\n", current_path);
        }

        DIR *dir = opendir(current_path);
        if (!dir) {
            fprintf(stderr, "OS: Error opening directory %s: %m\n", current_path);
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char *file_path = NULL;
            if (asprintf(&file_path, "%s/%s", current_path, entry->d_name) == -1) {
                fprintf(stderr, "Memory: Error allocating path for %s\n", entry->d_name);
                continue;
            }

            struct stat st;
            if (lstat(file_path, &st) == -1) {
                fprintf(stderr, "OS: Error getting file information for %s: %m\n", file_path);
                free(file_path);
                continue;
            }

            char filetype = S_ISLNK(st.st_mode) ? 'L' : (S_ISDIR(st.st_mode) ? 'D' : 'F');

            if (S_ISREG(st.st_mode)) {
                char extension[64];
                if (extension_allowed(entry->d_name, ext_list, ext_count, extension, sizeof(extension))) {
                    if (process_file(file_path, db, upsert_stmt, lookup_stmt, file_count, verbose, hash_files, hash_audio, force_rescan, filetype, &st, entry->d_name, extension) != 0) {
                        fprintf(stderr, "Error processing file: %s\n", file_path);
                    } else {
                        (*batch_count)++;
                    }

                    if (*batch_count >= BATCH_SIZE) {
                        if (commit_transaction(db) != 0 || begin_transaction(db) != 0) {
                            fprintf(stderr, "SQL: Error rotating transaction batch at %s\n", file_path);
                            free(file_path);
                            closedir(dir);
                            free_extensions(ext_list, ext_count);
                            destroy_dir_stack(stack);
                            return 1;
                        }
                        *batch_count = 0;
                    }
                }
            } else if (S_ISDIR(st.st_mode) && recurse_dirs) {
                push_dir(stack, file_path);
            }

            free(file_path);
        }

        closedir(dir);
    }

    free_extensions(ext_list, ext_count);
    destroy_dir_stack(stack);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Too few arguments: %s", USAGE_TEXT);
        return 1;
    }

    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "help") == 0) {
        help();
        return 0;
    }

    enum { CMD_UNKNOWN = 0, CMD_SCAN, CMD_DUPE, CMD_LINK } command = CMD_UNKNOWN;
    int arg_index = 1;
    if (strcmp(argv[arg_index], "scan") == 0) command = CMD_SCAN;
    else if (strcmp(argv[arg_index], "dupe") == 0) command = CMD_DUPE;
    else if (strcmp(argv[arg_index], "link") == 0) command = CMD_LINK;
    else if (strcmp(argv[arg_index], "help") == 0) {
        help();
        return 0;
    }
    else {
        fprintf(stderr, "Unknown command: %s\n%s", argv[arg_index], USAGE_TEXT);
        return 1;
    }
    arg_index++;

    int mainret=0;
    int verbose = 0;
    int force_rescan = 0;
    int hash_files = 0;
    int hash_audio = 0;
    int recurse_dirs = 0;
    int dupe_mode = 0;
    int min_dupes = 2;
    int link_mode = LINK_NONE;
    int dry_run = 0;
    char *database_path = "./file_hashes.db";
    char *start_path = NULL;
    char *extensions_concatenated = "";

    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[arg_index], "-dry") == 0) {
            dry_run = 1;
        } else if (strcmp(argv[arg_index], "-d") == 0) {
            if (arg_index + 1 < argc) {
                database_path = argv[++arg_index];
            } else {
                printf("Error: Missing argument for -d option\n");
                return 1;
            }
        } else if (strcmp(argv[arg_index], "-s") == 0) {
            if (arg_index + 1 < argc) {
                start_path = argv[++arg_index];
            } else {
                printf("Error: Missing argument for -s option\n");
                return 1;
            }
        } else if (strcmp(argv[arg_index], "-e") == 0) {
            if (arg_index + 1 < argc) {
                extensions_concatenated = argv[++arg_index];
            } else {
                printf("Error: Missing argument for -e option\n");
                return 1;
            }
        } else if (strcmp(argv[arg_index], "-r") == 0) {
            recurse_dirs = 1;
        } else if (strcmp(argv[arg_index], "-f") == 0) {
            force_rescan = 1;
        } else if (strcmp(argv[arg_index], "-h") == 0) {
            hash_files = 1;
        } else if (strcmp(argv[arg_index], "-a") == 0) {
            hash_audio = 1;
        } else if (strncmp(argv[arg_index], "-xa", 3) == 0 ||
                   strncmp(argv[arg_index], "-xh", 3) == 0) {
            int requested_mode = (argv[arg_index][2] == 'a') ? DUPE_AUDIO : DUPE_FILE;
            if (dupe_mode != 0 && dupe_mode != requested_mode) {
                fprintf(stderr, "Error: Duplicate flags are mutually exclusive (-xa vs -xh)\n");
                return 1;
            }
            dupe_mode = requested_mode;
            if (strlen(argv[arg_index]) > 3) {
                int candidate = atoi(argv[arg_index] + 3);
                min_dupes = (candidate > 1) ? candidate : min_dupes;
            }
        } else if (strncmp(argv[arg_index], "-l", 2) == 0) {
            if (strlen(argv[arg_index]) < 3) {
                fprintf(stderr, "Error: -l requires a mode (s,d,m,o,n)\n");
                return 1;
            }
            switch (argv[arg_index][2]) {
                case 's': link_mode = LINK_SHALLOW; break;
                case 'd': link_mode = LINK_DEEP; break;
                case 'm': link_mode = LINK_METADATA; break;
                case 'o': link_mode = LINK_OLDEST; break;
                case 'n': link_mode = LINK_NEWEST; break;
                default:
                    fprintf(stderr, "Error: Unknown -l mode '%c' (use s,d,m,o,n)\n", argv[arg_index][2]);
                    return 1;
            }
        } else if (strcmp(argv[arg_index], "-help") == 0 || strcmp(argv[arg_index], "help") == 0) {
            help();
            return 0;
        } else {
            fprintf(stderr, "Error: unknown option: %s\n%s", argv[arg_index], USAGE_TEXT);
            return 1;
        }
        arg_index++;
    }

    if (command == CMD_SCAN) {
        if (dupe_mode != 0 || link_mode != LINK_NONE) {
            fprintf(stderr, "Error: duplicate/link flags not allowed with scan\n");
            return 1;
        }
    } else if (command == CMD_DUPE) {
        if (dupe_mode == 0) {
            fprintf(stderr, "Error: dupe requires -xa or -xh\n");
            return 1;
        }
        if (link_mode != LINK_NONE || hash_files || hash_audio || force_rescan) {
            fprintf(stderr, "Error: scanning/link flags are not valid in dupe mode\n");
            return 1;
        }
    } else if (command == CMD_LINK) {
        if (dupe_mode == 0) {
            fprintf(stderr, "Error: link requires -xa or -xh\n");
            return 1;
        }
        if (link_mode == LINK_NONE) {
            fprintf(stderr, "Error: link requires -l{mode}\n");
            return 1;
        }
        if (hash_files || hash_audio || force_rescan) {
            fprintf(stderr, "Error: scanning flags are not valid in link mode\n");
            return 1;
        }
    }

    init_logging_callback(verbose);

    if (verbose) {
        printf("fhash version: %s (DB schema: %s)\n", FHASH_VERSION, DB_VERSION);
    }

    sqlite3 *db;
    if (sqlite3_open(database_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    if (ensure_schema_and_version(db) != 0) {
        sqlite3_close(db);
        return 1;
    }

    if (command == CMD_DUPE || command == CMD_LINK) {
        char resolved_filter[PATH_MAX] = {0};
        const char *path_filter = NULL;
        if (start_path) {
            if (realpath(start_path, resolved_filter) == NULL) {
                perror("Error resolving filter path");
                sqlite3_close(db);
                return 1;
            }
            path_filter = resolved_filter;
        }

        char **ext_list = NULL;
        int ext_count = 0;
        if (parse_extensions(extensions_concatenated, &ext_list, &ext_count) != 0) {
            sqlite3_close(db);
            return 1;
        }

        process_duplicates(db, dupe_mode, min_dupes, (command == CMD_LINK) ? link_mode : LINK_NONE, dry_run, path_filter, recurse_dirs, ext_list, ext_count);

        free_extensions(ext_list, ext_count);
        sqlite3_close(db);
        return 0;
    }

    if (start_path == NULL) start_path = ".";

    char resolved_dir[PATH_MAX];
    if (realpath(start_path, resolved_dir) == NULL) {
        perror("Error resolving directory path");
        sqlite3_close(db);
        return 1;
    }

    if (begin_transaction(db) != 0) {
        sqlite3_close(db);
        return 1;
    }

    const char *upsert_sql =
        "INSERT INTO files (md5, audio_md5, filepath, filename, extension, filesize, last_check_timestamp, modified_timestamp, filetype) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(filepath) DO UPDATE SET "
        "md5 = CASE WHEN ? THEN excluded.md5 ELSE files.md5 END, "
        "audio_md5 = CASE WHEN ? THEN excluded.audio_md5 ELSE files.audio_md5 END, "
        "filename = excluded.filename, "
        "extension = excluded.extension, "
        "filesize = excluded.filesize, "
        "last_check_timestamp = excluded.last_check_timestamp, "
        "modified_timestamp = excluded.modified_timestamp, "
        "filetype = excluded.filetype;";

    sqlite3_stmt *upsert_stmt = NULL;
    if (sqlite3_prepare_v2(db, upsert_sql, -1, &upsert_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare upsert statement: %s\n", sqlite3_errmsg(db));
        rollback_transaction(db);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_stmt *lookup_stmt = NULL;
    const char *lookup_sql = "SELECT filesize, modified_timestamp, filetype, md5, audio_md5 FROM files WHERE filepath = ?;";
    if (sqlite3_prepare_v2(db, lookup_sql, -1, &lookup_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare metadata lookup statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(upsert_stmt);
        rollback_transaction(db);
        sqlite3_close(db);
        return 1;
    }

    int file_count = 0;
    int batch_count = 0;

    if (process_directory(resolved_dir, db, upsert_stmt, lookup_stmt, &file_count, verbose, extensions_concatenated, force_rescan, &batch_count, hash_files, hash_audio, recurse_dirs) != 0) {
        mainret = 1;
    }

    if (mainret == 0) {
        if (commit_transaction(db) != 0) {
            mainret = 1;
        }
    } else {
        rollback_transaction(db);
    }

    sqlite3_finalize(upsert_stmt);
    sqlite3_finalize(lookup_stmt);
    sqlite3_close(db);

    if (verbose) {
        printf("Treated %d files.\n", file_count);
    }

    return mainret;
}
