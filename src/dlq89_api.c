#include "dlq89_priv.h"

#include <string.h>

static int db_valid(const dlq89_db *db)
{
    if (db == NULL || db->ops == NULL) return 0;
    if (db->ops->snapshot_open == NULL || db->ops->snapshot_close == NULL) return 0;
    if (db->ops->relation == NULL || db->ops->scan_open == NULL) return 0;
    if (db->ops->scan_next == NULL || db->ops->scan_close == NULL) return 0;
    return 1;
}

dlq89_status dlq89_parse(const unsigned char *source, size_t source_size,
                          dlq89_document **out,
                          dlq89_diagnostic *diagnostic)
{
    dlq89_document *document;
    dlq89_priv_parser parser;
    dlq89_status st;

    if (out == NULL || (source == NULL && source_size != 0))
    {
        return DLQ89_EINVAL;
    }
    *out = NULL;
    if (diagnostic != NULL)
    {
        memset(diagnostic, 0, sizeof(*diagnostic));
    }
    if (unicode89_utf8_valid(source, source_size) == 0)
    {
        dlq89_priv_diag(diagnostic, DLQ89_EUTF8, 0, 0, 1, 1, 0,
                        "invalid-utf8", "source is not valid UTF-8");
        return DLQ89_EUTF8;
    }
    document = (dlq89_document *)dlq89_priv_malloc(sizeof(*document));
    if (document == NULL)
    {
        return DLQ89_ENOMEM;
    }
    memset(document, 0, sizeof(*document));
    memset(&parser, 0, sizeof(parser));
    parser.s = source;
    parser.n = source_size;
    parser.line = 1;
    parser.column = 1;
    parser.diagnostic = diagnostic;
    st = dlq89_priv_parse(&parser, document);
    if (st != DLQ89_OK)
    {
        dlq89_document_destroy(document);
        return st;
    }
    *out = document;
    return DLQ89_OK;
}

void dlq89_document_destroy(dlq89_document *document)
{
    size_t i;

    if (document == NULL) return;
    for (i = 0; i < document->query_count; ++i)
    {
        dlq89_priv_query_destroy(&document->queries[i]);
    }
    dlq89_priv_free(document->queries);
    dlq89_priv_free(document);
}

dlq89_status dlq89_execute(const dlq89_document *document,
                            const dlq89_db *db, dlq89_cursor **out,
                            dlq89_diagnostic *diagnostic)
{
    dlq89_cursor *cursor;
    int rc;

    if (document == NULL || db_valid(db) == 0 || out == NULL)
    {
        return DLQ89_EINVAL;
    }
    *out = NULL;
    if (diagnostic != NULL)
    {
        memset(diagnostic, 0, sizeof(*diagnostic));
    }
    cursor = (dlq89_cursor *)dlq89_priv_malloc(sizeof(*cursor));
    if (cursor == NULL)
    {
        return DLQ89_ENOMEM;
    }
    memset(cursor, 0, sizeof(*cursor));
    cursor->document = document;
    cursor->db = *db;
    cursor->diagnostic = diagnostic;
    rc = db->ops->snapshot_open(db->ctx, &cursor->snapshot);
    if (rc != DLQ89_DB_OK || cursor->snapshot == NULL)
    {
        dlq89_priv_diag(diagnostic, DLQ89_EDATABASE, 0, 0, 0, 0, rc,
                        "snapshot-open", "database snapshot_open failed");
        dlq89_priv_free(cursor);
        return DLQ89_EDATABASE;
    }
    *out = cursor;
    return DLQ89_OK;
}

void dlq89_cursor_destroy(dlq89_cursor *cursor)
{
    if (cursor == NULL) return;
    dlq89_priv_result_reset(&cursor->result);
    if (cursor->snapshot != NULL)
    {
        cursor->db.ops->snapshot_close(cursor->db.ctx, cursor->snapshot);
    }
    dlq89_priv_free(cursor);
}

dlq89_status dlq89_cursor_next_result(dlq89_cursor *cursor,
                                       dlq89_result **result)
{
    dlq89_priv_query *q;
    dlq89_status st;

    if (cursor == NULL || result == NULL)
    {
        return DLQ89_EINVAL;
    }
    if (cursor->terminal < 0)
    {
        return cursor->terminal;
    }
    if (cursor->active_result != 0 && cursor->result.finished == 0)
    {
        return DLQ89_EINVAL;
    }
    if (cursor->next_query == cursor->document->query_count)
    {
        return DLQ89_END;
    }
    dlq89_priv_result_reset(&cursor->result);
    q = &cursor->document->queries[cursor->next_query];
    st = dlq89_priv_resolve(cursor, q);
    if (st != DLQ89_OK)
    {
        cursor->terminal = st;
        return st;
    }
    cursor->result.cursor = cursor;
    cursor->result.query = q;
    cursor->result.kind = q->var_count == 0 ? DLQ89_BOOLEAN : DLQ89_TABLE;
    cursor->active_result = 1;
    cursor->next_query += 1;
    if (cursor->result.kind == DLQ89_BOOLEAN)
    {
        st = dlq89_priv_materialize(&cursor->result);
        if (st != DLQ89_OK)
        {
            return st;
        }
        cursor->result.finished = 1;
    }
    *result = &cursor->result;
    return DLQ89_OK;
}

int dlq89_result_kindof(const dlq89_result *result)
{
    if (result == NULL) return 0;
    return result->kind;
}

size_t dlq89_result_column_count(const dlq89_result *result)
{
    if (result == NULL || result->kind != DLQ89_TABLE) return 0;
    return result->query->var_count;
}

const unsigned char *dlq89_result_column_name(const dlq89_result *result,
                                               size_t column, size_t *size)
{
    if (result == NULL || result->kind != DLQ89_TABLE ||
        column >= result->query->var_count)
    {
        return NULL;
    }
    if (size != NULL)
    {
        *size = result->query->vars[column].size;
    }
    return result->query->vars[column].data;
}

dlq89_status dlq89_result_next_row(dlq89_result *result,
                                    const dlq89_value **values)
{
    size_t c;
    size_t base;
    dlq89_status st;

    if (result == NULL || values == NULL || result->kind != DLQ89_TABLE)
    {
        return DLQ89_EINVAL;
    }
    if (result->terminal < 0)
    {
        return result->terminal;
    }
    st = dlq89_priv_materialize(result);
    if (st != DLQ89_OK)
    {
        return st;
    }
    if (result->row_index == result->row_count)
    {
        result->finished = 1;
        return DLQ89_END;
    }
    if (result->row_view == NULL)
    {
        result->row_view = (dlq89_value *)dlq89_priv_malloc(
            result->query->var_count * sizeof(*result->row_view));
        if (result->row_view == NULL)
        {
            result->terminal = DLQ89_ENOMEM;
            result->cursor->terminal = DLQ89_ENOMEM;
            return DLQ89_ENOMEM;
        }
    }
    base = result->row_index * result->query->var_count;
    for (c = 0; c < result->query->var_count; ++c)
    {
        result->row_view[c] = result->cells[base + c].value;
    }
    result->row_index += 1;
    *values = result->row_view;
    return DLQ89_OK;
}

int dlq89_result_boolean(const dlq89_result *result)
{
    if (result == NULL || result->kind != DLQ89_BOOLEAN ||
        result->terminal < 0 || result->materialized == 0)
    {
        return 0;
    }
    return result->boolean_value != 0;
}
