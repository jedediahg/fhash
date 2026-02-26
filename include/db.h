#ifndef DB_H
#define DB_H

#include <sqlite3.h>

int begin_transaction(sqlite3 *db);
int commit_transaction(sqlite3 *db);
int ensure_schema_and_version(sqlite3 *db);

#endif
