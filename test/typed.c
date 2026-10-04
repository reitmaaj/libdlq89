/* typed.c - DLQ scalar kinds: integers and strings never compare equal. */

#include <dlq89.h>

#include <stdlib.h>
#include <string.h>

struct dlq89_snapshot
{
    int unused;
};

struct dlq89_scan
{
    dlq89_relation relation;
    size_t index;
    int bound;
    dlq89_value value;
};

typedef struct fact
{
    dlq89_relation relation;
    dlq89_value_kind kind;
    const char *text;
} fact;

static const fact facts[] = {
    {1UL, DLQ89_VALUE_INTEGER, "42"},
    {1UL, DLQ89_VALUE_STRING, "42"},
    {2UL, DLQ89_VALUE_STRING, "1"},
    {2UL, DLQ89_VALUE_INTEGER, "1"}
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
    if (name_size == 3 && memcmp(name, "num", 3) == 0 && arity == 1)
    {
        *out = 1UL;
        return DLQ89_DB_OK;
    }
    if (name_size == 1 && name[0] == 's' && arity == 1)
    {
        *out = 2UL;
        return DLQ89_DB_OK;
    }
    return DLQ89_DB_NOT_FOUND;
}

static int scan_open(void *ctx, dlq89_snapshot *s, dlq89_relation rel,
                     size_t arity, const dlq89_value *values,
                     const unsigned char *bound, dlq89_scan **out)
{
    struct dlq89_scan *scan;

    (void)ctx;
    (void)s;
    (void)arity;
    scan = (struct dlq89_scan *)malloc(sizeof(*scan));
    if (scan == NULL)
    {
        return DLQ89_DB_ERROR;
    }
    scan->relation = rel;
    scan->index = 0;
    scan->bound = bound[0] != 0;
    scan->value = values[0];
    *out = scan;
    return DLQ89_DB_OK;
}

static int value_eq(const dlq89_value *a, const dlq89_value *b)
{
    if (a->kind != b->kind || a->size != b->size)
    {
        return 0;
    }
    if (a->size == 0)
    {
        return 1;
    }
    return memcmp(a->data, b->data, a->size) == 0;
}

static int scan_next(void *ctx, struct dlq89_scan *scan, dlq89_value *tuple,
                     int *found)
{
    const fact *f;
    dlq89_value want;

    (void)ctx;
    while (scan->index < sizeof(facts) / sizeof(facts[0]))
    {
        f = &facts[scan->index];
        scan->index += 1;
        if (f->relation != scan->relation)
        {
            continue;
        }
        if (scan->bound != 0)
        {
            want.kind = f->kind;
            want.data = (const unsigned char *)f->text;
            want.size = strlen(f->text);
            if (value_eq(&scan->value, &want) == 0)
            {
                continue;
            }
        }
        tuple[0].kind = f->kind;
        tuple[0].data = (const unsigned char *)f->text;
        tuple[0].size = strlen(f->text);
        *found = 1;
        return DLQ89_DB_OK;
    }
    *found = 0;
    return DLQ89_DB_OK;
}

static void scan_close(void *ctx, struct dlq89_scan *scan)
{
    (void)ctx;
    free(scan);
}

static int run_bool(const dlq89_db *db, const char *source, int *out)
{
    dlq89_document *doc;
    dlq89_cursor *cursor;
    dlq89_result *result;
    dlq89_diagnostic diagnostic;
    dlq89_status st;
    int rc;

    doc = NULL;
    if (dlq89_parse((const unsigned char *)source, strlen(source), &doc,
                    &diagnostic) != DLQ89_OK)
    {
        return -1;
    }
    cursor = NULL;
    if (dlq89_execute(doc, db, &cursor, &diagnostic) != DLQ89_OK)
    {
        dlq89_document_destroy(doc);
        return -2;
    }
    result = NULL;
    st = dlq89_cursor_next_result(cursor, &result);
    rc = -3;
    if (st == DLQ89_OK && dlq89_result_kindof(result) == DLQ89_BOOLEAN)
    {
        *out = dlq89_result_boolean(result);
        rc = 0;
    }
    dlq89_cursor_destroy(cursor);
    dlq89_document_destroy(doc);
    return rc;
}

static int run_count(const dlq89_db *db, const char *source, size_t *out)
{
    dlq89_document *doc;
    dlq89_cursor *cursor;
    dlq89_result *result;
    const dlq89_value *row;
    dlq89_diagnostic diagnostic;
    dlq89_status st;
    size_t count;
    int rc;

    doc = NULL;
    if (dlq89_parse((const unsigned char *)source, strlen(source), &doc,
                    &diagnostic) != DLQ89_OK)
    {
        return -1;
    }
    cursor = NULL;
    if (dlq89_execute(doc, db, &cursor, &diagnostic) != DLQ89_OK)
    {
        dlq89_document_destroy(doc);
        return -2;
    }
    result = NULL;
    st = dlq89_cursor_next_result(cursor, &result);
    count = 0;
    rc = -3;
    if (st == DLQ89_OK && dlq89_result_kindof(result) == DLQ89_TABLE)
    {
        while (dlq89_result_next_row(result, &row) == DLQ89_OK)
        {
            count += 1;
        }
        *out = count;
        rc = 0;
    }
    dlq89_cursor_destroy(cursor);
    dlq89_document_destroy(doc);
    return rc;
}

int main(void)
{
    static const dlq89_db_ops ops = {snapshot_open, snapshot_close, relation,
                                     scan_open, scan_next, scan_close};
    dlq89_db db;
    int flag;
    size_t count;

    db.ctx = NULL;
    db.ops = &ops;

    flag = 0;
    if (run_bool(&db, "?- num(42).", &flag) != 0 || flag == 0)
    {
        return 1;
    }
    flag = 0;
    if (run_bool(&db, "?- num(\"42\").", &flag) != 0 || flag == 0)
    {
        return 2;
    }
    flag = 1;
    if (run_bool(&db, "?- num(43).", &flag) != 0 || flag != 0)
    {
        return 3;
    }
    flag = 0;
    if (run_bool(&db, "?- num(042).", &flag) != 0 || flag == 0)
    {
        return 4;
    }
    count = 0;
    if (run_count(&db, "?- num(X).", &count) != 0 || count != 2)
    {
        return 5;
    }
    flag = 0;
    if (run_bool(&db, "?- s(1).", &flag) != 0 || flag == 0)
    {
        return 6;
    }
    flag = 0;
    if (run_bool(&db, "?- s(\"1\").", &flag) != 0 || flag == 0)
    {
        return 7;
    }
    flag = 1;
    if (run_bool(&db, "?- s(2).", &flag) != 0 || flag != 0)
    {
        return 8;
    }
    return 0;
}
