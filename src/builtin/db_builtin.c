#include "db_builtin.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/error.h"

static const char *g_db_names[] = { "dbOpen", "dbClose", "dbExec", "dbQuery", "dbGet" };

bool is_db_builtin(const char *name) {
    for (size_t i = 0; i < sizeof(g_db_names) / sizeof(g_db_names[0]); i++) {
        if (strcmp(name, g_db_names[i]) == 0) return true;
    }
    return false;
}

#ifndef NAC_WITH_SQLITE

Value call_db_builtin(const char *name, Value *args, int arg_count) {
    (void)args;
    (void)arg_count;
    char msg[300];
    snprintf(msg, sizeof(msg),
             "%s(): this build of NaC has no SQLite support (install the SQLite development "
             "package and rebuild; see the README)", name);
    report_error(msg);
    return make_null();
}

void nac_db_close_all(void) {}

#else /* NAC_WITH_SQLITE */

#include <sqlite3.h>

#define MAX_DB_HANDLES 16
#define DB_BUSY_TIMEOUT_MS 5000

/* Handles are small ints (slot + 1). Every call arrives with the interpreter
 * lock held, so the table needs no locking of its own. */
static sqlite3 *g_dbs[MAX_DB_HANDLES];

typedef enum { DB_EXEC, DB_QUERY, DB_GET } DbMode;

static void fail(const char *fn, const char *detail) {
    char msg[600];
    snprintf(msg, sizeof(msg), "%s(): %s", fn, detail);
    report_error(msg);
}

static void fail_db(const char *fn, sqlite3 *db) {
    fail(fn, sqlite3_errmsg(db));
}

static sqlite3 *lookup_db(const char *fn, Value handle) {
    if (handle.type != TYPE_INT || handle.int_val < 1 || handle.int_val > MAX_DB_HANDLES ||
        !g_dbs[handle.int_val - 1]) {
        fail(fn, "the first argument must be an open database handle from dbOpen()");
        return NULL;
    }
    return g_dbs[handle.int_val - 1];
}

static void array_append(Value *arr, Value item) {
    if (arr->array_val.size >= arr->array_val.capacity) {
        int new_cap = arr->array_val.capacity == 0 ? 8 : arr->array_val.capacity * 2;
        arr->array_val.elements = (Value*)realloc(arr->array_val.elements, sizeof(Value) * (size_t)new_cap);
        arr->array_val.capacity = new_cap;
    }
    arr->array_val.elements[arr->array_val.size++] = item;   /* takes ownership */
}

/* ---- binding NaC values to placeholders ------------------------------- */

static int bind_value(const char *fn, sqlite3_stmt *stmt, int index, Value v) {
    int rc;
    switch (v.type) {
        case TYPE_INT:    rc = sqlite3_bind_int64(stmt, index, (sqlite3_int64)v.int_val); break;
        case TYPE_FLOAT:  rc = sqlite3_bind_double(stmt, index, v.float_val); break;
        case TYPE_BOOL:   rc = sqlite3_bind_int64(stmt, index, v.int_val ? 1 : 0); break;
        case TYPE_NULL:   rc = sqlite3_bind_null(stmt, index); break;
        case TYPE_STRING: rc = sqlite3_bind_text(stmt, index, v.str_val, -1, SQLITE_TRANSIENT); break;
        default:
            fail(fn, "only text, numbers, booleans and null can be bound as parameters");
            return 0;
    }
    if (rc != SQLITE_OK) {
        fail(fn, "could not bind a parameter");
        return 0;
    }
    return 1;
}

static int bind_params(const char *fn, sqlite3_stmt *stmt, const Value *params) {
    int needed = sqlite3_bind_parameter_count(stmt);
    char msg[300];

    if (!params) {
        if (needed > 0) {
            snprintf(msg, sizeof(msg), "the SQL has %d placeholder(s) but no parameters were given", needed);
            fail(fn, msg);
            return 0;
        }
        return 1;
    }

    if (params->type == TYPE_ARRAY) {
        if (params->array_val.size != needed) {
            snprintf(msg, sizeof(msg), "the SQL has %d placeholder(s) but %d parameter(s) were given",
                     needed, params->array_val.size);
            fail(fn, msg);
            return 0;
        }
        for (int i = 0; i < needed; i++) {
            if (!bind_value(fn, stmt, i + 1, params->array_val.elements[i])) return 0;
        }
        return 1;
    }

    if (params->type == TYPE_MAP) {
        Value lookup = *params;     /* shallow copy, only used for map_get */
        for (int i = 1; i <= needed; i++) {
            const char *pname = sqlite3_bind_parameter_name(stmt, i);
            if (!pname || !pname[0] || !pname[1] || pname[0] == '?') {
                fail(fn, "a map of parameters needs named placeholders (:name), not '?'");
                return 0;
            }
            Value *v = map_get(&lookup, pname + 1);
            if (!v) {
                snprintf(msg, sizeof(msg), "no value for the placeholder %s", pname);
                fail(fn, msg);
                return 0;
            }
            if (!bind_value(fn, stmt, i, *v)) return 0;
        }
        return 1;
    }

    fail(fn, "parameters must be an array (for ?) or a map (for :name)");
    return 0;
}

/* ---- turning result columns into NaC values --------------------------- */

static int column_value(const char *fn, sqlite3_stmt *stmt, int col, Value *out) {
    char msg[300];
    switch (sqlite3_column_type(stmt, col)) {
        case SQLITE_INTEGER: {
            sqlite3_int64 v = sqlite3_column_int64(stmt, col);
            /* NaC ints are 32-bit: larger integers become floats. */
            if (v >= -2147483647LL - 1 && v <= 2147483647LL) *out = make_int((int)v);
            else *out = make_float((double)v);
            return 1;
        }
        case SQLITE_FLOAT:
            *out = make_float(sqlite3_column_double(stmt, col));
            return 1;
        case SQLITE_TEXT: {
            int bytes = sqlite3_column_bytes(stmt, col);
            const unsigned char *text = sqlite3_column_text(stmt, col);
            *out = text ? make_string_len((const char*)text, (size_t)bytes) : make_string("");
            return 1;
        }
        case SQLITE_BLOB:
            snprintf(msg, sizeof(msg),
                     "column '%s' is a BLOB, which NaC cannot hold (use hex(%s) or CAST(%s AS TEXT))",
                     sqlite3_column_name(stmt, col), sqlite3_column_name(stmt, col),
                     sqlite3_column_name(stmt, col));
            fail(fn, msg);
            return 0;
        default:
            *out = make_null();
            return 1;
    }
}

static int build_row(const char *fn, sqlite3_stmt *stmt, Value *row) {
    *row = make_map();
    int cols = sqlite3_column_count(stmt);
    for (int c = 0; c < cols; c++) {
        Value v;
        if (!column_value(fn, stmt, c, &v)) {
            free_value(row);
            return 0;
        }
        const char *name = sqlite3_column_name(stmt, c);
        map_set(row, name ? name : "", v);
        free_value(&v);
    }
    return 1;
}

/* True when `tail` holds another SQL statement (comments and ';' do not count). */
static int has_more_statements(sqlite3 *db, const char *tail) {
    sqlite3_stmt *stmt = NULL;
    const char *next = NULL;
    int rc = sqlite3_prepare_v2(db, tail, -1, &stmt, &next);
    if (stmt) sqlite3_finalize(stmt);
    return rc != SQLITE_OK || stmt != NULL;
}

/* ---- dbExec / dbQuery / dbGet ----------------------------------------- */

static Value run_sql(const char *fn, Value *args, int arg_count, DbMode mode) {
    if (arg_count < 2 || arg_count > 3 || args[1].type != TYPE_STRING) {
        char msg[200];
        snprintf(msg, sizeof(msg), "requires (db, sql [, params]) with a string for sql");
        fail(fn, msg);
        return make_null();
    }
    sqlite3 *db = lookup_db(fn, args[0]);
    if (!db) return make_null();

    const Value *params = (arg_count == 3 && args[2].type != TYPE_NULL) ? &args[2] : NULL;
    const char *tail = args[1].str_val;
    /* Rows changed by the statements themselves. sqlite3_total_changes() would
     * also count rows removed by ON DELETE CASCADE and trigger bodies, so a
     * DELETE of one row could report 2. */
    int direct_changes = 0;

    Value rows = make_array(0);
    Value first_row = make_null();
    int failed = 0;
    int statements = 0;

    for (;;) {
        while (*tail && isspace((unsigned char)*tail)) tail++;
        if (!*tail) break;

        sqlite3_stmt *stmt = NULL;
        const char *next = tail;
        if (sqlite3_prepare_v2(db, tail, -1, &stmt, &next) != SQLITE_OK) {
            fail_db(fn, db);
            failed = 1;
            break;
        }
        if (!stmt) {                     /* only a comment or ';' */
            if (next == tail) break;
            tail = next;
            continue;
        }
        statements++;

        /* Parameters and queries are for exactly one statement; check before
         * running anything so that nothing half-executes. */
        if ((params || mode != DB_EXEC) && has_more_statements(db, next)) {
            fail(fn, mode == DB_EXEC
                         ? "parameters can only be used with a single SQL statement"
                         : "only one SQL statement is allowed here");
            sqlite3_finalize(stmt);
            failed = 1;
            break;
        }

        if (!bind_params(fn, stmt, params)) {
            sqlite3_finalize(stmt);
            failed = 1;
            break;
        }

        int changes_before = sqlite3_total_changes(db);
        for (;;) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                if (mode == DB_EXEC) continue;
                if (mode == DB_QUERY && rows.array_val.size >= MAX_ARRAY_SIZE) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                             "more than %d rows; narrow the query (WHERE, LIMIT/OFFSET)", MAX_ARRAY_SIZE);
                    fail(fn, msg);
                    failed = 1;
                    break;
                }
                Value row;
                if (!build_row(fn, stmt, &row)) {
                    failed = 1;
                    break;
                }
                if (mode == DB_GET) {
                    first_row = row;
                    break;
                }
                array_append(&rows, row);
            } else if (rc == SQLITE_DONE) {
                break;
            } else {
                fail_db(fn, db);
                failed = 1;
                break;
            }
        }
        /* sqlite3_changes() is the count of the last INSERT/UPDATE/DELETE, so
         * it is only this statement's when something actually changed. */
        if (!failed && sqlite3_total_changes(db) != changes_before) {
            direct_changes += sqlite3_changes(db);
        }
        sqlite3_finalize(stmt);
        if (failed || mode == DB_GET) break;
        tail = next;
    }

    if (failed) {
        free_value(&rows);
        free_value(&first_row);
        return make_null();
    }

    if (mode == DB_QUERY) {
        free_value(&first_row);
        return rows;
    }
    free_value(&rows);
    if (mode == DB_GET) return first_row;

    Value result = make_map();
    Value changes = make_int(direct_changes);
    Value last_id = make_int((int)sqlite3_last_insert_rowid(db));
    map_set(&result, "changes", changes);
    map_set(&result, "lastId", last_id);
    return result;
}

/* ---- dbOpen / dbClose -------------------------------------------------- */

static Value db_open(Value *args, int arg_count) {
    if (arg_count != 1 || args[0].type != TYPE_STRING || !args[0].str_val[0]) {
        fail("dbOpen", "requires (path) with a non-empty string, e.g. dbOpen(\"app.db\") or dbOpen(\":memory:\")");
        return make_null();
    }

    int slot = -1;
    for (int i = 0; i < MAX_DB_HANDLES; i++) {
        if (!g_dbs[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        fail("dbOpen", "too many open databases (close one with dbClose())");
        return make_null();
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(args[0].str_val, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) {
        char msg[600];
        snprintf(msg, sizeof(msg), "cannot open '%.200s': %s", args[0].str_val,
                 db ? sqlite3_errmsg(db) : "out of memory");
        fail("dbOpen", msg);
        if (db) sqlite3_close(db);
        return make_null();
    }

    /* Sensible defaults: wait for a locked database instead of failing at
     * once, and enforce FOREIGN KEY constraints (SQLite leaves them off). */
    sqlite3_busy_timeout(db, DB_BUSY_TIMEOUT_MS);
    sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

    g_dbs[slot] = db;
    return make_int(slot + 1);
}

static Value db_close(Value *args, int arg_count) {
    if (arg_count != 1) {
        fail("dbClose", "requires 1 argument (db)");
        return make_int(1);
    }
    if (!lookup_db("dbClose", args[0])) return make_int(1);
    int slot = args[0].int_val - 1;
    sqlite3_close(g_dbs[slot]);
    g_dbs[slot] = NULL;
    return make_int(0);
}

Value call_db_builtin(const char *name, Value *args, int arg_count) {
    if (strcmp(name, "dbOpen") == 0) return db_open(args, arg_count);
    if (strcmp(name, "dbClose") == 0) return db_close(args, arg_count);
    if (strcmp(name, "dbExec") == 0) return run_sql(name, args, arg_count, DB_EXEC);
    if (strcmp(name, "dbQuery") == 0) return run_sql(name, args, arg_count, DB_QUERY);
    if (strcmp(name, "dbGet") == 0) return run_sql(name, args, arg_count, DB_GET);
    return make_null();
}

void nac_db_close_all(void) {
    for (int i = 0; i < MAX_DB_HANDLES; i++) {
        if (g_dbs[i]) {
            sqlite3_close(g_dbs[i]);
            g_dbs[i] = NULL;
        }
    }
}

#endif /* NAC_WITH_SQLITE */
