#include <dlq89.h>

#include <stdio.h>
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
    dlq89_value values[2];
    unsigned char bound[2];
};

typedef struct fact
{
    dlq89_relation relation;
    size_t arity;
    const char *v0;
    const char *v1;
} fact;

static const fact facts[] = {
    {1UL, 2, "a", "b"},
    {1UL, 2, "a", "b"},
    {1UL, 2, "b", "c"},
    {2UL, 0, NULL, NULL}
};

static struct dlq89_snapshot snapshot;

static int value_matches(const dlq89_value *v, const char *s)
{
    size_t n;

    n = strlen(s);
    return v->kind == DLQ89_VALUE_STRING && v->size == n &&
           memcmp(v->data, s, n) == 0;
}

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
    if (name_size == 4 && memcmp(name, "edge", 4) == 0 && arity == 2)
    {
        *out = 1UL;
        return DLQ89_DB_OK;
    }
    if (name_size == 5 && memcmp(name, "ready", 5) == 0 && arity == 0)
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
    size_t i;

    (void)ctx;
    (void)s;
    scan = (struct dlq89_scan *)malloc(sizeof(*scan));
    if (scan == NULL)
    {
        return DLQ89_DB_ERROR;
    }
    scan->relation = rel;
    scan->index = 0;
    for (i = 0; i < arity; ++i)
    {
        scan->values[i] = values[i];
        scan->bound[i] = bound[i];
    }
    *out = scan;
    return DLQ89_DB_OK;
}

static int fact_matches(const struct dlq89_scan *scan, const fact *f)
{
    if (scan->relation != f->relation)
    {
        return 0;
    }
    if (f->arity > 0 && scan->bound[0] != 0 &&
        value_matches(&scan->values[0], f->v0) == 0)
    {
        return 0;
    }
    if (f->arity > 1 && scan->bound[1] != 0 &&
        value_matches(&scan->values[1], f->v1) == 0)
    {
        return 0;
    }
    return 1;
}

static int scan_next(void *ctx, dlq89_scan *scan, dlq89_value *tuple,
                     int *found)
{
    const fact *f;

    (void)ctx;
    while (scan->index < sizeof(facts) / sizeof(facts[0]))
    {
        f = &facts[scan->index];
        scan->index += 1;
        if (fact_matches(scan, f) == 0)
        {
            continue;
        }
        if (f->arity > 0)
        {
            tuple[0].kind = DLQ89_VALUE_STRING;
            tuple[0].data = (const unsigned char *)f->v0;
            tuple[0].size = strlen(f->v0);
        }
        if (f->arity > 1)
        {
            tuple[1].kind = DLQ89_VALUE_STRING;
            tuple[1].data = (const unsigned char *)f->v1;
            tuple[1].size = strlen(f->v1);
        }
        *found = 1;
        return DLQ89_DB_OK;
    }
    *found = 0;
    return DLQ89_DB_OK;
}

static void scan_close(void *ctx, dlq89_scan *scan)
{
    (void)ctx;
    free(scan);
}

static int same(const dlq89_value *v, const char *s)
{
    return value_matches(v, s);
}

int main(void)
{
    static const dlq89_db_ops ops = {
        snapshot_open, snapshot_close, relation, scan_open, scan_next, scan_close
    };
    static const unsigned char source[] =
        "?- edge(X, Y), edge(Y, Z).\n"
        "?- ready().\n"
        "?- edge(X, \"b\").\n";
    dlq89_db db;
    dlq89_document *doc;
    dlq89_cursor *cursor;
    dlq89_result *result;
    const dlq89_value *row;
    dlq89_diagnostic diagnostic;
    size_t n;
    const unsigned char *name;
    dlq89_status st;

    db.ctx = NULL;
    db.ops = &ops;
    doc = NULL;
    st = dlq89_parse(source, sizeof(source) - 1, &doc, &diagnostic);
    if (st != DLQ89_OK)
    {
        return 1;
    }
    cursor = NULL;
    st = dlq89_execute(doc, &db, &cursor, &diagnostic);
    if (st != DLQ89_OK)
    {
        return 2;
    }
    result = NULL;
    st = dlq89_cursor_next_result(cursor, &result);
    if (st != DLQ89_OK || dlq89_result_kindof(result) != DLQ89_TABLE)
    {
        return 3;
    }
    if (dlq89_result_column_count(result) != 3)
    {
        return 4;
    }
    name = dlq89_result_column_name(result, 2, &n);
    if (name == NULL || n != 1 || name[0] != 'Z')
    {
        return 5;
    }
    st = dlq89_cursor_next_result(cursor, &result);
    if (st != DLQ89_EINVAL)
    {
        return 6;
    }
    st = dlq89_result_next_row(result, &row);
    if (st != DLQ89_OK)
    {
        return 7;
    }
    if (!same(&row[0], "a") || !same(&row[1], "b") ||
        !same(&row[2], "c"))
    {
        return 8;
    }
    st = dlq89_result_next_row(result, &row);
    if (st != DLQ89_END)
    {
        return 9;
    }
    st = dlq89_cursor_next_result(cursor, &result);
    if (st != DLQ89_OK || dlq89_result_kindof(result) != DLQ89_BOOLEAN)
    {
        return 10;
    }
    if (dlq89_result_boolean(result) == 0)
    {
        return 11;
    }
    st = dlq89_cursor_next_result(cursor, &result);
    if (st != DLQ89_OK || dlq89_result_column_count(result) != 1)
    {
        return 12;
    }
    st = dlq89_result_next_row(result, &row);
    if (st != DLQ89_OK || !same(&row[0], "a"))
    {
        return 13;
    }
    st = dlq89_result_next_row(result, &row);
    if (st != DLQ89_END)
    {
        return 14;
    }
    st = dlq89_cursor_next_result(cursor, &result);
    if (st != DLQ89_END)
    {
        return 15;
    }
    dlq89_cursor_destroy(cursor);
    dlq89_document_destroy(doc);
    return 0;
}
