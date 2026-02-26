#include "db.h"
#include "fhash.h"
#include <stdio.h>
#include <string.h>

static int ensure_filetype_column(sqlite3 *db) {
    int has_column = 0;
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(files);", -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *col_name = sqlite3_column_text(stmt, 1);
            if (col_name && strcmp((const char *)col_name, "filetype") == 0) {
                has_column = 1;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    if (!has_column) {
        if (sqlite3_exec(db, "ALTER TABLE files ADD COLUMN filetype TEXT DEFAULT 'F';", NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQL error adding filetype column: %s\n", sqlite3_errmsg(db));
            return 1;
        }
    }
    return 0;
}

static int ensure_modified_column(sqlite3 *db) {
    int has_column = 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(files);", -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *col_name = sqlite3_column_text(stmt, 1);
            if (col_name && strcmp((const char *)col_name, "modified_timestamp") == 0) {
                has_column = 1;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    if (!has_column) {
        if (sqlite3_exec(db, "ALTER TABLE files ADD COLUMN modified_timestamp INTEGER DEFAULT 0;", NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQL error adding modified_timestamp column: %s\n", sqlite3_errmsg(db));
            return 1;
        }
    }
    return 0;
}

int ensure_schema_and_version(sqlite3 *db) {
    if (sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS sys (key TEXT PRIMARY KEY, value TEXT);", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error ensuring sys table: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    int has_db_version = 0;
    int has_version = 0;
    char db_ver_buf[64] = {0};
    char app_ver_buf[64] = {0};
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "SELECT value FROM sys WHERE key = ?;", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, "db_version", -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *val = sqlite3_column_text(stmt, 0);
            if (val) {
                has_db_version = 1;
                strncpy(db_ver_buf, (const char *)val, sizeof(db_ver_buf) - 1);
            }
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_text(stmt, 1, "version", -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            has_version = 1;
            const unsigned char *val = sqlite3_column_text(stmt, 0);
            if (val) {
                strncpy(app_ver_buf, (const char *)val, sizeof(app_ver_buf) - 1);
            }
        }
    }
    sqlite3_finalize(stmt);

    if (has_db_version && strcmp(db_ver_buf, DB_VERSION) != 0) {
        fprintf(stderr, "Database version mismatch: db has %s, fhash requires %s\n", db_ver_buf, DB_VERSION);
        return 1;
    }
    if (has_version && strcmp(app_ver_buf, FHASH_VERSION) != 0) {
        fprintf(stderr, "fhash version mismatch recorded in DB: db has %s, binary is %s\n", app_ver_buf, FHASH_VERSION);
        return 1;
    }
    if (!has_db_version || !has_version) {
        char insert_sql[256];
        snprintf(insert_sql, sizeof(insert_sql), "INSERT OR REPLACE INTO sys (key, value) VALUES ('version', '%s'), ('db_version', '%s');", FHASH_VERSION, DB_VERSION);
        if (sqlite3_exec(db, insert_sql, NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQL error inserting sys version rows: %s\n", sqlite3_errmsg(db));
            return 1;
        }
    }

    const char *create_files_sql =
        "CREATE TABLE IF NOT EXISTS files ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "md5 TEXT, "
        "audio_md5 TEXT, "
        "filepath TEXT, "
        "filename TEXT, "
        "extension TEXT, "
        "filesize INTEGER, "
        "last_check_timestamp TIMESTAMP, "
        "modified_timestamp INTEGER DEFAULT 0, "
        "filetype TEXT DEFAULT 'F', "
        "UNIQUE(filepath)"
        ");";
    if (sqlite3_exec(db, create_files_sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error ensuring files table: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    if (ensure_filetype_column(db) != 0) {
        return 1;
    }
    if (ensure_modified_column(db) != 0) {
        return 1;
    }

    if (sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_files_md5 ON files(md5);", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error creating idx_files_md5: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    if (sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_files_audio_md5 ON files(audio_md5);", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error creating idx_files_audio_md5: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    if (sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_files_extension ON files(extension);", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error creating idx_files_extension: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    return 0;
}

int begin_transaction(sqlite3 *db) {
    if (sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to begin transaction: %s\n", sqlite3_errmsg(db));
        return 1; 
    }
    return 0; 
}

int commit_transaction(sqlite3 *db) {
    if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to commit transaction: %s\n", sqlite3_errmsg(db));
        return 1; 
    }
    return 0; 
}

int rollback_transaction(sqlite3 *db) {
    if (sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to rollback transaction: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    return 0;
}
