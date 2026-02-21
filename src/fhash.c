#include "common.h"
#include "utils.h"
#include "hashing.h"
#include "db.h"
#include <sqlite3.h>
#include <libavutil/log.h>

int process_file(const char *file_path, sqlite3 *db, sqlite3_stmt *bulk_stmt, sqlite3_stmt *update_stmt, int *file_count, int verbose, int hash_file, int hash_audio, int action) {
    if (action < 1 || action > 2){
        fprintf(stderr, "Invalid file action: %d\n", action);
        return 1;
    }

    char md5_string[MD5_DIGEST_LENGTH * 2 + 1] = {0};
    if (hash_file) {
        unsigned char md5_hash[MD5_DIGEST_LENGTH];
        if (calculate_md5(file_path, md5_hash) != 0) {
            fprintf(stderr, "Error calculating MD5 hash for file: %s\n", file_path);
            return 1;
        }

        if (strcmp((char *)md5_hash, "0-byte-file") == 0) {
            strncpy(md5_string, "0-byte-file", sizeof(md5_string));
        } else {
            for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                snprintf(&md5_string[i * 2], 3, "%02x", (unsigned int)md5_hash[i]);
            }
        }
    } else {
        snprintf(md5_string, sizeof(md5_string), "Not calculated");
    }

    char audio_md5_string[MD5_DIGEST_LENGTH * 2 + 1] = {0};
    if (hash_audio) {
        unsigned char audio_md5_hash[MD5_DIGEST_LENGTH];
        if (calculate_audio_md5(file_path, audio_md5_hash) != 0) {
            snprintf(audio_md5_string, sizeof(audio_md5_string), "N/A");
        } else {
            for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                snprintf(&audio_md5_string[i * 2], 3, "%02x", (unsigned int)audio_md5_hash[i]);
            }
        }
    } else {
        snprintf(audio_md5_string, sizeof(audio_md5_string), "Not calculated");
    }

    const char *filename = strrchr(file_path, '/');
    filename = (filename != NULL) ? (filename + 1) : file_path;
    char *extension = strrchr(filename, '.');
    extension = (extension != NULL) ? (extension + 1) : "";
    struct stat st;
    if (stat(file_path, &st) != 0) {
        fprintf(stderr, "OS: Error getting file information for %s: %m\n", file_path);
        return 1;
    }

    int64_t filesize = (int64_t)st.st_size;
    time_t current_time = time(NULL);

    if (verbose) {
        printf("\tMD5: %s\n", md5_string);
        printf("\tAudio MD5: %s\n", audio_md5_string);
        printf("\tFilepath: %s\n", file_path);
        printf("\tFilename: %s\n", filename);
        printf("\tExtension: %s\n", extension);
        printf("\tFilesize: %ld\n", (long)filesize);
        printf("\tTimestamp: %ld\n", (long)current_time);
    }

    sqlite3_stmt *stmt = (action == INSERT_ACTION) ? bulk_stmt : update_stmt;
    if (action == INSERT_ACTION) {
        sqlite3_bind_text(stmt, 1, md5_string, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, audio_md5_string, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, file_path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, filename, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, extension, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, filesize);
        sqlite3_bind_int64(stmt, 7, current_time);
    } else if (action == UPDATE_ACTION) {
        int param_idx = 1;
        if (hash_file) {
            sqlite3_bind_text(stmt, param_idx++, md5_string, -1, SQLITE_TRANSIENT);
        }
        if (hash_audio) {
            sqlite3_bind_text(stmt, param_idx++, audio_md5_string, -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int64(stmt, param_idx++, filesize);
        sqlite3_bind_int64(stmt, param_idx++, current_time);
        sqlite3_bind_text(stmt, param_idx++, file_path, -1, SQLITE_TRANSIENT);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "SQL: Error executing statement for %s: %s\n", file_path, sqlite3_errmsg(db));
        return 1;
    } else {
        (*file_count)++;
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        if (verbose) {
            printf("Processed file: %s (Action: %d, Hashed: %d)\n", file_path, action, hash_file);
        }
    }
    return 0;
}

int process_directory(const char *dir_path, sqlite3 *db, sqlite3_stmt *bulk_stmt, sqlite3_stmt *update_stmt, int *file_count, int verbose, const char *extensions_concatenated, int force_rescan, int *batch_count, int hash_files, int hash_audio, int recurse_dirs) {
    DirStack *stack = create_dir_stack(STACK_SIZE); 
    push_dir(stack, dir_path);

    // Parse extension list once
    char **ext_list = NULL;
    int ext_count = 0;
    if (extensions_concatenated && strlen(extensions_concatenated) > 0) {
        char *ext_copy = strdup(extensions_concatenated);
        char *token = strtok(ext_copy, ",");
        while (token) {
            char **tmp = realloc(ext_list, sizeof(char *) * (ext_count + 1));
            if (!tmp) {
                fprintf(stderr, "Memory: Error reallocating extension list\n");
                break; 
            }
            ext_list = tmp;
            ext_list[ext_count++] = strdup(token);
            token = strtok(NULL, ",");
        }
        free(ext_copy);
    }

    sqlite3_stmt *select_stmt;
    const char *select_sql = "SELECT filepath FROM files WHERE filepath = ?;";
    if (sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL: Error preparing select statement: %s\n", sqlite3_errmsg(db));
        for (int i = 0; i < ext_count; i++) free(ext_list[i]);
        free(ext_list);
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

            if (S_ISREG(st.st_mode)) {
                char *filename = entry->d_name;
                char *extension = strrchr(filename, '.');
                if (extension) {
                    extension++; // Skip the dot
                    
                    int match = 0;
                    for (int i = 0; i < ext_count; i++) {
                        if (strcasecmp(ext_list[i], extension) == 0) {
                            match = 1;
                            break;
                        }
                    }

                    if (match) {
                        int action = 0;
                        sqlite3_bind_text(select_stmt, 1, file_path, -1, SQLITE_TRANSIENT);
                        int result = sqlite3_step(select_stmt);
                        if (result == SQLITE_ROW) {
                            if (force_rescan) {
                                action = UPDATE_ACTION;
                            }
                        } else if (result == SQLITE_DONE) {
                            action = INSERT_ACTION;
                        } else {
                            fprintf(stderr, "SQL: Error during select: %s\n", sqlite3_errmsg(db));
                        }
                        sqlite3_reset(select_stmt);
                        sqlite3_clear_bindings(select_stmt);

                        if (action > 0) {
                            if (process_file(file_path, db, bulk_stmt, update_stmt, file_count, verbose, hash_files, hash_audio, action) != 0) {
                                fprintf(stderr, "Error processing file: %s\n", file_path);
                            } else {
                                (*batch_count)++;
                            }
                        }

                        if (*batch_count >= BATCH_SIZE) {
                            commit_transaction(db);
                            begin_transaction(db);
                            *batch_count = 0;
                        }
                    }
                }
            } else if (S_ISDIR(st.st_mode) && recurse_dirs) {
                push_dir(stack, file_path);
            }
            free(file_path);
        }
        closedir(dir);
    }

    sqlite3_finalize(select_stmt);
    for (int i = 0; i < ext_count; i++) free(ext_list[i]);
    free(ext_list);
    destroy_dir_stack(stack);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Too few arguments: %s", USAGE_TEXT);
        return 1;
    }

    int mainret=0;
    int verbose = 0;
    int arg_index = 1;
    int force_rescan = 0;
    int hash_files = 0;
    int hash_audio = 0;
    int recurse_dirs = 0;
    char *database_path = "./file_hashes.db";
    char *start_path = ".";
    char *extensions_concatenated = "";

    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[arg_index], "-help") == 0) {
            help();
            return 0;
        } else if (strcmp(argv[arg_index], "-f") == 0) {
            force_rescan = 1;
        } else if (strcmp(argv[arg_index], "-h") == 0) {
            hash_files = 1;
        } else if (strcmp(argv[arg_index], "-a") == 0) {
            hash_audio = 1;
        } else if (strcmp(argv[arg_index], "-r") == 0) {
            recurse_dirs = 1;
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
        } else {
            break;
        }
        arg_index++;
    }

    if (argc != arg_index) {
        printf("Error, unkown options, %s:", USAGE_TEXT);
        return 1;
    }

    if (verbose) {
        av_log_set_level(AV_LOG_INFO);
    } else {
        av_log_set_level(AV_LOG_ERROR);
    }

    char resolved_dir[PATH_MAX];
    if (realpath(start_path, resolved_dir) == NULL) {
        perror("Error resolving directory path");
        return 1;
    }

    sqlite3 *db;
    if (sqlite3_open(database_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    char *sql = "CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY AUTOINCREMENT, md5 TEXT, audio_md5 TEXT, filepath TEXT, filename TEXT, extension TEXT, filesize INTEGER, last_check_timestamp TIMESTAMP, UNIQUE(filepath));";
    if (sqlite3_exec(db, sql, 0, 0, 0) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    if (begin_transaction(db) != 0) {
        sqlite3_close(db);
        return 1;
    }
    const char *bulk_insert_sql = "INSERT OR IGNORE INTO files (md5, audio_md5, filepath, filename, extension, filesize, last_check_timestamp) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *bulk_stmt;
    if (sqlite3_prepare_v2(db, bulk_insert_sql, -1, &bulk_stmt, 0) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    char sqlroot[MAX_PATH_LENGTH + 256]; 
    strcpy(sqlroot, "UPDATE files SET ");
    int first = 1;
    if (hash_files) {
        strcat(sqlroot, "md5 = ?");
        first = 0;
    }
    if (hash_audio) {
        if (!first) strcat(sqlroot, ", ");
        strcat(sqlroot, "audio_md5 = ?");
        first = 0;
    }
    if (!first) strcat(sqlroot, ", ");
    strcat(sqlroot, "filesize = ?, last_check_timestamp = ? WHERE filepath = ?;");

    sqlite3_stmt *update_stmt;
    if (sqlite3_prepare_v2(db, sqlroot, -1, &update_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare update statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    int file_count = 0;
    int batch_count = 0;

    if (process_directory(resolved_dir, db, bulk_stmt, update_stmt, &file_count, verbose, extensions_concatenated, force_rescan, &batch_count, hash_files, hash_audio, recurse_dirs) != 0) {
        mainret = 1;
    }

    if (commit_transaction(db) != 0) {
        mainret = 1;
    }

    sqlite3_finalize(bulk_stmt);
    sqlite3_finalize(update_stmt);
    sqlite3_close(db);

    if (verbose) {
        printf("Treated %d files.\n", file_count);
    }
    return mainret;
}
