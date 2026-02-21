#include "db.h"
#include <stdio.h>

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
