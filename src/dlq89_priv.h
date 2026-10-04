#ifndef DLQ89_PRIV_H
#define DLQ89_PRIV_H

#include <dlq89.h>
#include <unicode89.h>
#include <unicode89/identifier.h>
#include <unicode89/normalize.h>

#include <stddef.h>

void *dlq89_priv_malloc(size_t n);
void *dlq89_priv_realloc(void *p, size_t n);
void dlq89_priv_free(void *p);

typedef struct dlq89_priv_bytes
{
    unsigned char *data;
    size_t size;
} dlq89_priv_bytes;

typedef struct dlq89_priv_term
{
    int is_var;
    size_t var;
    dlq89_value value;
    dlq89_priv_bytes storage;
} dlq89_priv_term;

typedef struct dlq89_priv_atom
{
    dlq89_priv_bytes name;
    size_t arity;
    dlq89_priv_term *terms;
    dlq89_relation relation;
} dlq89_priv_atom;

typedef struct dlq89_priv_query
{
    dlq89_priv_bytes *vars;
    size_t var_count;
    dlq89_priv_atom *atoms;
    size_t atom_count;
} dlq89_priv_query;

struct dlq89_document
{
    dlq89_priv_query *queries;
    size_t query_count;
};

typedef struct dlq89_priv_cell
{
    dlq89_value value;
    dlq89_priv_bytes storage;
} dlq89_priv_cell;

struct dlq89_result
{
    struct dlq89_cursor *cursor;
    dlq89_priv_query *query;
    int kind;
    int boolean_value;
    int materialized;
    int finished;
    dlq89_status terminal;
    dlq89_priv_cell *cells;
    size_t row_count;
    size_t row_cap;
    size_t row_index;
    dlq89_value *row_view;
};

struct dlq89_cursor
{
    const dlq89_document *document;
    dlq89_db db;
    dlq89_snapshot *snapshot;
    size_t next_query;
    dlq89_result result;
    int active_result;
    dlq89_status terminal;
    dlq89_diagnostic *diagnostic;
};

typedef struct dlq89_priv_parser
{
    const unsigned char *s;
    size_t n;
    size_t p;
    size_t line;
    size_t column;
    dlq89_diagnostic *diagnostic;
} dlq89_priv_parser;

dlq89_status dlq89_priv_parse(dlq89_priv_parser *parser,
                               dlq89_document *document);
void dlq89_priv_query_destroy(dlq89_priv_query *query);
dlq89_status dlq89_priv_resolve(dlq89_cursor *cursor, dlq89_priv_query *query);
dlq89_status dlq89_priv_materialize(dlq89_result *result);
void dlq89_priv_result_reset(dlq89_result *result);
void dlq89_priv_diag(dlq89_diagnostic *diagnostic, dlq89_status status,
                     size_t offset, size_t end_offset, size_t line,
                     size_t column, int database_status, const char *code,
                     const char *message);

#endif
