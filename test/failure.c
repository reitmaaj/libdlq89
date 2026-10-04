#include <dlq89.h>

#include <stdlib.h>
#include <string.h>

struct dlq89_snapshot
{
    int unused;
};

struct dlq89_scan
{
    int failed;
};

static struct dlq89_snapshot snapshot;

static int snapshot_open(void *ctx, dlq89_snapshot **out)
{
    (void)ctx;
    *out = &snapshot;
    return DLQ89_DB_OK;
}

static void snapshot_close(void *ctx, dlq89_snapshot *s)
{
    (void)ctx;
    (void)s;
}

static int relation(void *ctx, dlq89_snapshot *s, const unsigned char *name,
                    size_t name_size, size_t arity, dlq89_relation *out)
{
    (void)ctx;
    (void)s;
    if (name_size == 1 && name[0] == 'p' && arity == 1)
    {
        *out = 1UL;
        return DLQ89_DB_OK;
    }
    return DLQ89_DB_NOT_FOUND;
}

static int scan_open(void *ctx, dlq89_snapshot *s, dlq89_relation relation_id,
                     size_t arity, const dlq89_value *values,
                     const unsigned char *bound, dlq89_scan **out)
{
    struct dlq89_scan *scan;

    (void)ctx;
    (void)s;
    (void)relation_id;
    (void)arity;
    (void)values;
    (void)bound;
    scan = (struct dlq89_scan *)malloc(sizeof(*scan));
    if (scan == NULL)
    {
        return DLQ89_DB_ERROR;
    }
    scan->failed = 0;
    *out = scan;
    return DLQ89_DB_OK;
}

static int scan_next(void *ctx, dlq89_scan *scan, dlq89_value *tuple,
                     int *found)
{
    (void)ctx;
    (void)tuple;
    (void)found;
    if (scan->failed == 0)
    {
        scan->failed = 1;
        return 77;
    }
    return DLQ89_DB_ERROR;
}

static void scan_close(void *ctx, dlq89_scan *scan)
{
    (void)ctx;
    free(scan);
}

int main(void)
{
    static const dlq89_db_ops ops = {
        snapshot_open, snapshot_close, relation, scan_open, scan_next, scan_close
    };
    static const unsigned char source[] = "?- p(X). ?- p(Y).";
    dlq89_db db;
    dlq89_document *doc;
    dlq89_cursor *cursor;
    dlq89_result *result;
    const dlq89_value *row;
    dlq89_diagnostic diagnostic;
    dlq89_status st;

    db.ctx = NULL;
    db.ops = &ops;
    doc = NULL;
    if (dlq89_parse(source, sizeof(source) - 1, &doc, &diagnostic) !=
        DLQ89_OK)
    {
        return 1;
    }
    cursor = NULL;
    if (dlq89_execute(doc, &db, &cursor, &diagnostic) != DLQ89_OK)
    {
        return 2;
    }
    result = NULL;
    if (dlq89_cursor_next_result(cursor, &result) != DLQ89_OK)
    {
        return 3;
    }
    row = (const dlq89_value *)1;
    st = dlq89_result_next_row(result, &row);
    if (st != DLQ89_EDATABASE)
    {
        return 4;
    }
    if (row != (const dlq89_value *)1)
    {
        return 5;
    }
    if (diagnostic.database_status != 77)
    {
        return 6;
    }
    if (dlq89_cursor_next_result(cursor, &result) != DLQ89_EDATABASE)
    {
        return 7;
    }
    dlq89_cursor_destroy(cursor);
    dlq89_document_destroy(doc);
    return 0;
}
