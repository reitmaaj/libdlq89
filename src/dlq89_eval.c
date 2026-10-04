#include "dlq89_priv.h"

#include <string.h>

typedef struct eval_state
{
    dlq89_result *result;
    dlq89_priv_query *query;
    dlq89_value *env;
    unsigned char *bound;
    size_t *log;
    dlq89_value *scan_values;
    unsigned char *scan_bound;
    dlq89_value *tuple;
    int stop;
} eval_state;

static int value_equal(const dlq89_value *a, const dlq89_value *b)
{
    if (a->kind != b->kind)
    {
        return 0;
    }
    if (a->size != b->size)
    {
        return 0;
    }
    if (a->size == 0)
    {
        return 1;
    }
    return memcmp(a->data, b->data, a->size) == 0;
}

static dlq89_status copy_cell(dlq89_priv_cell *cell, const dlq89_value *value)
{
    unsigned char *p;

    p = (unsigned char *)dlq89_priv_malloc(value->size);
    if (p == NULL && value->size != 0)
    {
        return DLQ89_ENOMEM;
    }
    if (value->size != 0)
    {
        memcpy(p, value->data, value->size);
    }
    cell->storage.data = p;
    cell->storage.size = value->size;
    cell->value.kind = value->kind;
    cell->value.data = p;
    cell->value.size = value->size;
    return DLQ89_OK;
}

static int row_equal(const dlq89_result *r, size_t row,
                     const dlq89_value *values)
{
    size_t c;
    size_t base;

    base = row * r->query->var_count;
    for (c = 0; c < r->query->var_count; ++c)
    {
        if (value_equal(&r->cells[base + c].value, &values[c]) == 0)
        {
            return 0;
        }
    }
    return 1;
}

static dlq89_status append_row(eval_state *s)
{
    dlq89_priv_cell *cells;
    size_t i;
    size_t old_count;
    size_t new_cap;
    size_t total;
    dlq89_status st;

    if (s->query->var_count == 0)
    {
        s->result->boolean_value = 1;
        s->stop = 1;
        return DLQ89_OK;
    }
    for (i = 0; i < s->result->row_count; ++i)
    {
        if (row_equal(s->result, i, s->env) != 0)
        {
            return DLQ89_OK;
        }
    }
    if (s->result->row_count == s->result->row_cap)
    {
        new_cap = s->result->row_cap == 0 ? 8 : s->result->row_cap * 2;
        if (new_cap < s->result->row_cap)
        {
            return DLQ89_ENOMEM;
        }
        if (s->query->var_count != 0 &&
            new_cap > ((size_t)-1) / s->query->var_count)
        {
            return DLQ89_ENOMEM;
        }
        total = new_cap * s->query->var_count;
        cells = (dlq89_priv_cell *)dlq89_priv_realloc(
            s->result->cells, total * sizeof(*cells));
        if (cells == NULL)
        {
            return DLQ89_ENOMEM;
        }
        old_count = s->result->row_cap * s->query->var_count;
        memset(cells + old_count, 0, (total - old_count) * sizeof(*cells));
        s->result->cells = cells;
        s->result->row_cap = new_cap;
    }
    old_count = s->result->row_count * s->query->var_count;
    for (i = 0; i < s->query->var_count; ++i)
    {
        st = copy_cell(&s->result->cells[old_count + i], &s->env[i]);
        if (st != DLQ89_OK)
        {
            while (i != 0)
            {
                i -= 1;
                dlq89_priv_free(s->result->cells[old_count + i].storage.data);
                memset(&s->result->cells[old_count + i], 0,
                       sizeof(s->result->cells[old_count + i]));
            }
            return st;
        }
    }
    s->result->row_count += 1;
    return DLQ89_OK;
}

static dlq89_status join(eval_state *s, size_t depth);

static void build_scan(eval_state *s, const dlq89_priv_atom *a)
{
    size_t i;
    const dlq89_priv_term *t;

    for (i = 0; i < a->arity; ++i)
    {
        t = &a->terms[i];
        if (t->is_var == 0)
        {
            s->scan_values[i] = t->value;
            s->scan_bound[i] = 1;
        }
        else if (s->bound[t->var] != 0)
        {
            s->scan_values[i] = s->env[t->var];
            s->scan_bound[i] = 1;
        }
        else
        {
            s->scan_values[i].data = NULL;
            s->scan_values[i].size = 0;
            s->scan_bound[i] = 0;
        }
    }
}

static int bind_tuple(eval_state *s, const dlq89_priv_atom *a,
                      size_t *bound_count)
{
    size_t i;
    size_t n;
    const dlq89_priv_term *t;

    n = 0;
    for (i = 0; i < a->arity; ++i)
    {
        t = &a->terms[i];
        if (t->is_var == 0)
        {
            if (value_equal(&t->value, &s->tuple[i]) == 0)
            {
                break;
            }
        }
        else if (s->bound[t->var] != 0)
        {
            if (value_equal(&s->env[t->var], &s->tuple[i]) == 0)
            {
                break;
            }
        }
        else
        {
            s->env[t->var] = s->tuple[i];
            s->bound[t->var] = 1;
            s->log[n] = t->var;
            n += 1;
        }
    }
    *bound_count = n;
    return i == a->arity;
}

static void unbind(eval_state *s, size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i)
    {
        s->bound[s->log[i]] = 0;
    }
}

static dlq89_status scan_atom(eval_state *s, size_t depth)
{
    dlq89_cursor *cursor;
    dlq89_priv_atom *a;
    dlq89_scan *scan;
    size_t bound_count;
    int found;
    int rc;
    int matched;
    dlq89_status st;

    cursor = s->result->cursor;
    a = &s->query->atoms[depth];
    build_scan(s, a);
    scan = NULL;
    rc = cursor->db.ops->scan_open(cursor->db.ctx, cursor->snapshot,
                                   a->relation, a->arity, s->scan_values,
                                   s->scan_bound, &scan);
    if (rc != DLQ89_DB_OK || scan == NULL)
    {
        dlq89_priv_diag(cursor->diagnostic, DLQ89_EDATABASE, 0, 0, 0, 0, rc,
                        "scan-open", "database scan_open failed");
        return DLQ89_EDATABASE;
    }
    st = DLQ89_OK;
    for (;;)
    {
        found = 0;
        rc = cursor->db.ops->scan_next(cursor->db.ctx, scan, s->tuple, &found);
        if (rc != DLQ89_DB_OK)
        {
            dlq89_priv_diag(cursor->diagnostic, DLQ89_EDATABASE, 0, 0, 0, 0,
                            rc, "scan-next", "database scan_next failed");
            st = DLQ89_EDATABASE;
            break;
        }
        if (found == 0 || s->stop != 0)
        {
            break;
        }
        bound_count = 0;
        matched = bind_tuple(s, a, &bound_count);
        if (matched != 0)
        {
            st = join(s, depth + 1);
        }
        unbind(s, bound_count);
        if (st != DLQ89_OK || s->stop != 0)
        {
            break;
        }
    }
    cursor->db.ops->scan_close(cursor->db.ctx, scan);
    return st;
}

static dlq89_status join(eval_state *s, size_t depth)
{
    if (depth == s->query->atom_count)
    {
        return append_row(s);
    }
    return scan_atom(s, depth);
}

static size_t max_arity(const dlq89_priv_query *q)
{
    size_t i;
    size_t n;

    n = 0;
    for (i = 0; i < q->atom_count; ++i)
    {
        if (q->atoms[i].arity > n)
        {
            n = q->atoms[i].arity;
        }
    }
    return n;
}

dlq89_status dlq89_priv_resolve(dlq89_cursor *cursor, dlq89_priv_query *q)
{
    size_t i;
    int rc;

    for (i = 0; i < q->atom_count; ++i)
    {
        rc = cursor->db.ops->relation(cursor->db.ctx, cursor->snapshot,
                                      q->atoms[i].name.data,
                                      q->atoms[i].name.size, q->atoms[i].arity,
                                      &q->atoms[i].relation);
        if (rc == DLQ89_DB_NOT_FOUND)
        {
            dlq89_priv_diag(cursor->diagnostic, DLQ89_EQUERY, 0, 0, 0, 0, rc,
                            "unknown-relation", "relation name/arity not found");
            return DLQ89_EQUERY;
        }
        if (rc != DLQ89_DB_OK)
        {
            dlq89_priv_diag(cursor->diagnostic, DLQ89_EDATABASE, 0, 0, 0, 0,
                            rc, "relation-lookup",
                            "database relation lookup failed");
            return DLQ89_EDATABASE;
        }
    }
    return DLQ89_OK;
}

dlq89_status dlq89_priv_materialize(dlq89_result *r)
{
    eval_state s;
    size_t arity;
    dlq89_status st;

    if (r->materialized != 0)
    {
        return r->terminal;
    }
    memset(&s, 0, sizeof(s));
    s.result = r;
    s.query = r->query;
    arity = max_arity(r->query);
    s.env = (dlq89_value *)dlq89_priv_malloc(
        r->query->var_count * sizeof(*s.env));
    s.bound = (unsigned char *)dlq89_priv_malloc(r->query->var_count);
    s.log = (size_t *)dlq89_priv_malloc(arity * sizeof(*s.log));
    s.scan_values = (dlq89_value *)dlq89_priv_malloc(
        arity * sizeof(*s.scan_values));
    s.scan_bound = (unsigned char *)dlq89_priv_malloc(arity);
    s.tuple = (dlq89_value *)dlq89_priv_malloc(arity * sizeof(*s.tuple));
    if ((s.env == NULL && r->query->var_count != 0) ||
        (s.bound == NULL && r->query->var_count != 0) ||
        (s.log == NULL && arity != 0) ||
        (s.scan_values == NULL && arity != 0) ||
        (s.scan_bound == NULL && arity != 0) ||
        (s.tuple == NULL && arity != 0))
    {
        st = DLQ89_ENOMEM;
    }
    else
    {
        if (r->query->var_count != 0)
        {
            memset(s.bound, 0, r->query->var_count);
        }
        st = join(&s, 0);
    }
    dlq89_priv_free(s.tuple);
    dlq89_priv_free(s.scan_bound);
    dlq89_priv_free(s.scan_values);
    dlq89_priv_free(s.log);
    dlq89_priv_free(s.bound);
    dlq89_priv_free(s.env);
    r->materialized = 1;
    r->terminal = st;
    if (st < 0)
    {
        r->cursor->terminal = st;
    }
    return st;
}

void dlq89_priv_result_reset(dlq89_result *r)
{
    size_t i;
    size_t n;

    n = 0;
    if (r->query != NULL)
    {
        n = r->row_count * r->query->var_count;
    }
    for (i = 0; i < n; ++i)
    {
        dlq89_priv_free(r->cells[i].storage.data);
    }
    dlq89_priv_free(r->cells);
    dlq89_priv_free(r->row_view);
    memset(r, 0, sizeof(*r));
}
