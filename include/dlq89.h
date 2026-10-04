#ifndef DLQ89_H
#define DLQ89_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dlq89_status
{
    DLQ89_OK = 0,
    DLQ89_END = 1,
    DLQ89_EINVAL = -1,
    DLQ89_ENOMEM = -2,
    DLQ89_EUTF8 = -3,
    DLQ89_ESYNTAX = -4,
    DLQ89_EQUERY = -5,
    DLQ89_EDATABASE = -6,
    DLQ89_EINTERNAL = -7
} dlq89_status;

const char *dlq89_status_name(dlq89_status status);
const char *dlq89_status_message(dlq89_status status);

/* A DLQ scalar is either an exact UTF-8 string (no normalization) or an
 * arbitrary-precision integer in canonical ASCII: no leading '+', no leading
 * zero except the value zero itself, and no sign on zero. The two kinds never
 * compare equal, so 42 and "42" are distinct values. */
typedef enum dlq89_value_kind
{
    DLQ89_VALUE_STRING = 1,
    DLQ89_VALUE_INTEGER = 2
} dlq89_value_kind;

typedef struct dlq89_value
{
    dlq89_value_kind kind;
    const unsigned char *data;
    size_t size;
} dlq89_value;

typedef unsigned long dlq89_relation;
typedef struct dlq89_snapshot dlq89_snapshot;
typedef struct dlq89_scan dlq89_scan;

typedef enum dlq89_db_status
{
    DLQ89_DB_OK = 0,
    DLQ89_DB_NOT_FOUND = 1,
    DLQ89_DB_ERROR = -1
} dlq89_db_status;

typedef struct dlq89_db_ops
{
    int (*snapshot_open)(void *ctx, dlq89_snapshot **snapshot);
    void (*snapshot_close)(void *ctx, dlq89_snapshot *snapshot);
    int (*relation)(void *ctx, dlq89_snapshot *snapshot,
                    const unsigned char *name, size_t name_size, size_t arity,
                    dlq89_relation *relation);
    int (*scan_open)(void *ctx, dlq89_snapshot *snapshot,
                     dlq89_relation relation, size_t arity,
                     const dlq89_value *values, const unsigned char *bound,
                     dlq89_scan **scan);
    int (*scan_next)(void *ctx, dlq89_scan *scan, dlq89_value *tuple,
                     int *found);
    void (*scan_close)(void *ctx, dlq89_scan *scan);
} dlq89_db_ops;

typedef struct dlq89_db
{
    void *ctx;
    const dlq89_db_ops *ops;
} dlq89_db;

typedef struct dlq89_diagnostic
{
    dlq89_status status;
    size_t offset;
    size_t end_offset;
    size_t line;
    size_t column;
    int database_status;
    const char *code;
    const char *message;
} dlq89_diagnostic;

typedef struct dlq89_document dlq89_document;
typedef struct dlq89_cursor dlq89_cursor;
typedef struct dlq89_result dlq89_result;

typedef enum dlq89_result_kind
{
    DLQ89_TABLE = 1,
    DLQ89_BOOLEAN = 2
} dlq89_result_kind;

dlq89_status dlq89_parse(const unsigned char *source, size_t source_size,
                          dlq89_document **out,
                          dlq89_diagnostic *diagnostic);
void dlq89_document_destroy(dlq89_document *document);

dlq89_status dlq89_execute(const dlq89_document *document,
                            const dlq89_db *db, dlq89_cursor **out,
                            dlq89_diagnostic *diagnostic);
void dlq89_cursor_destroy(dlq89_cursor *cursor);

dlq89_status dlq89_cursor_next_result(dlq89_cursor *cursor,
                                       dlq89_result **result);
int dlq89_result_kindof(const dlq89_result *result);
size_t dlq89_result_column_count(const dlq89_result *result);
const unsigned char *dlq89_result_column_name(const dlq89_result *result,
                                               size_t column, size_t *size);
dlq89_status dlq89_result_next_row(dlq89_result *result,
                                    const dlq89_value **values);
int dlq89_result_boolean(const dlq89_result *result);

#ifdef __cplusplus
}
#endif

#endif
