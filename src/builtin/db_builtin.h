#ifndef NAC_DB_BUILTIN_H
#define NAC_DB_BUILTIN_H

#include <stdbool.h>

#include "../runtime/value.h"

/* SQLite builtins:
 *
 *   db = dbOpen(path)                  open (or create) a database file;
 *                                      ":memory:" for a throw-away one. Returns
 *                                      a handle (an int), or null on failure.
 *   dbExec(db, sql [, params])         run INSERT/UPDATE/DELETE/CREATE/...
 *                                      -> { changes, lastId }
 *   dbQuery(db, sql [, params])        run a SELECT -> array of row maps
 *   dbGet(db, sql [, params])          first row of a SELECT as a map, or null
 *   dbClose(db)
 *
 * `params` binds values to the SQL's placeholders, so values never get pasted
 * into the SQL text: an array for positional "?" placeholders, or a map for
 * named ":name" ones. Text, numbers, booleans and null can be bound; a failing
 * statement reports an error the script can catch with try/catch.
 *
 * Compiled in only when NAC_WITH_SQLITE is defined (build.sh and build.bat do
 * that when SQLite is available). Without it the functions still exist and
 * report that this build has no SQLite. */
bool is_db_builtin(const char *name);
Value call_db_builtin(const char *name, Value *args, int arg_count);

/* Closes every open database; called when the interpreter shuts down. */
void nac_db_close_all(void);

#endif
