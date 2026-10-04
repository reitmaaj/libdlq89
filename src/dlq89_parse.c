#include "dlq89_priv.h"

#include <string.h>

static int is_ws(unsigned char c)
{
    return c == 0x20 || c == 0x09 || c == 0x0a || c == 0x0d;
}

static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static void step(dlq89_priv_parser *p)
{
    unsigned char c;

    c = p->s[p->p];
    p->p += 1;
    if (c == '\n')
    {
        p->line += 1;
        p->column = 1;
    }
    else
    {
        p->column += 1;
    }
}

static void skip_ws(dlq89_priv_parser *p)
{
    while (p->p < p->n && is_ws(p->s[p->p]))
    {
        step(p);
    }
}

static dlq89_status syntax(dlq89_priv_parser *p, const char *code,
                           const char *message)
{
    dlq89_priv_diag(p->diagnostic, DLQ89_ESYNTAX, p->p, p->p, p->line,
                    p->column, 0, code, message);
    return DLQ89_ESYNTAX;
}

static int bytes_equal(const dlq89_priv_bytes *a, const dlq89_priv_bytes *b)
{
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

static dlq89_status normalize_identifier(const unsigned char *s, size_t n,
                                         dlq89_priv_bytes *out)
{
    size_t work_n;
    size_t out_n;
    unicode89_cp *work;
    unsigned char *buf;
    int rc;

    work_n = 0;
    rc = unicode89_normalize_work_bound(s, n, &work_n);
    if (rc < 0)
    {
        return DLQ89_EUTF8;
    }
    work = (unicode89_cp *)dlq89_priv_malloc(work_n * sizeof(*work));
    if (work == NULL && work_n != 0)
    {
        return DLQ89_ENOMEM;
    }
    rc = unicode89_normalize_ex(0, s, n, NULL, 0, work, work_n);
    if (rc < 0)
    {
        dlq89_priv_free(work);
        return rc == UNICODE89_EUTF8 ? DLQ89_EUTF8 : DLQ89_EINTERNAL;
    }
    out_n = (size_t)rc;
    buf = (unsigned char *)dlq89_priv_malloc(out_n);
    if (buf == NULL && out_n != 0)
    {
        dlq89_priv_free(work);
        return DLQ89_ENOMEM;
    }
    rc = unicode89_normalize_ex(0, s, n, buf, out_n, work, work_n);
    dlq89_priv_free(work);
    if (rc < 0 || (size_t)rc != out_n)
    {
        dlq89_priv_free(buf);
        return rc == UNICODE89_EUTF8 ? DLQ89_EUTF8 : DLQ89_EINTERNAL;
    }
    out->data = buf;
    out->size = out_n;
    return DLQ89_OK;
}

static dlq89_status identifier(dlq89_priv_parser *p, dlq89_priv_bytes *out)
{
    size_t start;
    size_t pos;
    size_t next;
    unicode89_cp cp;
    int first;
    dlq89_status st;
    int rc;

    start = p->p;
    pos = p->p;
    first = 1;
    while (pos < p->n)
    {
        next = pos;
        cp = 0;
        rc = unicode89_utf8_decode(p->s, p->n, pos, &cp, &next);
        if (rc != UNICODE89_OK)
        {
            return DLQ89_EUTF8;
        }
        if (first != 0)
        {
            if (unicode89_identifier_xid_start(cp) == 0)
            {
                break;
            }
            first = 0;
        }
        else if (unicode89_identifier_xid_continue(cp) == 0)
        {
            break;
        }
        pos = next;
    }
    if (first != 0)
    {
        return syntax(p, "expected-identifier", "expected identifier");
    }
    while (p->p < pos)
    {
        step(p);
    }
    st = normalize_identifier(p->s + start, pos - start, out);
    if (st == DLQ89_EUTF8)
    {
        dlq89_priv_diag(p->diagnostic, st, start, pos, p->line, p->column, 0,
                        "invalid-utf8", "invalid UTF-8 in identifier");
    }
    return st;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static dlq89_status append_byte(unsigned char **buf, size_t *n, size_t *cap,
                                unsigned char c)
{
    unsigned char *q;
    size_t next_cap;

    if (*n == *cap)
    {
        next_cap = *cap == 0 ? 16 : *cap * 2;
        if (next_cap < *cap)
        {
            return DLQ89_ENOMEM;
        }
        q = (unsigned char *)dlq89_priv_realloc(*buf, next_cap);
        if (q == NULL)
        {
            return DLQ89_ENOMEM;
        }
        *buf = q;
        *cap = next_cap;
    }
    (*buf)[*n] = c;
    *n += 1;
    return DLQ89_OK;
}

static dlq89_status append_cp(unsigned char **buf, size_t *n, size_t *cap,
                              unicode89_cp cp)
{
    unsigned char tmp[4];
    int len;
    int i;
    dlq89_status st;

    len = unicode89_utf8_encode(cp, tmp);
    if (len == 0)
    {
        return DLQ89_ESYNTAX;
    }
    for (i = 0; i < len; ++i)
    {
        st = append_byte(buf, n, cap, tmp[i]);
        if (st != DLQ89_OK)
        {
            return st;
        }
    }
    return DLQ89_OK;
}

static dlq89_status parse_u4(dlq89_priv_parser *p, unsigned int *out)
{
    int h;
    int i;
    unsigned int v;

    if (p->n - p->p < 4)
    {
        return syntax(p, "short-unicode-escape", "short Unicode escape");
    }
    v = 0;
    for (i = 0; i < 4; ++i)
    {
        h = hex_value(p->s[p->p]);
        if (h < 0)
        {
            return syntax(p, "bad-unicode-escape", "invalid Unicode escape");
        }
        v = v * 16U + (unsigned int)h;
        step(p);
    }
    *out = v;
    return DLQ89_OK;
}

static dlq89_status string_escape(dlq89_priv_parser *p, unsigned char **buf,
                                  size_t *n, size_t *cap)
{
    unsigned char c;
    unsigned int a;
    unsigned int b;
    unicode89_cp cp;
    dlq89_status st;

    if (p->p >= p->n)
    {
        return syntax(p, "unterminated-string", "unterminated string");
    }
    c = p->s[p->p];
    step(p);
    if (c == '"' || c == '\\' || c == '/') return append_byte(buf, n, cap, c);
    if (c == 'b') return append_byte(buf, n, cap, 0x08);
    if (c == 'f') return append_byte(buf, n, cap, 0x0c);
    if (c == 'n') return append_byte(buf, n, cap, 0x0a);
    if (c == 'r') return append_byte(buf, n, cap, 0x0d);
    if (c == 't') return append_byte(buf, n, cap, 0x09);
    if (c != 'u')
    {
        return syntax(p, "bad-escape", "invalid string escape");
    }
    st = parse_u4(p, &a);
    if (st != DLQ89_OK) return st;
    if (a >= 0xd800U && a <= 0xdbffU)
    {
        if (p->n - p->p < 2 || p->s[p->p] != '\\' || p->s[p->p + 1] != 'u')
        {
            return syntax(p, "unpaired-surrogate", "unpaired high surrogate");
        }
        step(p);
        step(p);
        st = parse_u4(p, &b);
        if (st != DLQ89_OK) return st;
        if (unicode89_utf16_decode_pair(a, b, &cp) == 0)
        {
            return syntax(p, "unpaired-surrogate", "invalid surrogate pair");
        }
        return append_cp(buf, n, cap, cp);
    }
    if (a >= 0xdc00U && a <= 0xdfffU)
    {
        return syntax(p, "unpaired-surrogate", "unpaired low surrogate");
    }
    return append_cp(buf, n, cap, (unicode89_cp)a);
}

static dlq89_status string_literal(dlq89_priv_parser *p,
                                   dlq89_priv_term *term)
{
    unsigned char *buf;
    size_t n;
    size_t cap;
    unsigned char c;
    size_t next;
    unicode89_cp cp;
    int rc;
    dlq89_status st;

    if (p->p >= p->n || p->s[p->p] != '"')
    {
        return syntax(p, "expected-term", "expected variable or string");
    }
    step(p);
    buf = NULL;
    n = 0;
    cap = 0;
    while (p->p < p->n)
    {
        c = p->s[p->p];
        if (c == '"')
        {
            step(p);
            term->is_var = 0;
            term->storage.data = buf;
            term->storage.size = n;
            term->value.kind = DLQ89_VALUE_STRING;
            term->value.data = buf;
            term->value.size = n;
            return DLQ89_OK;
        }
        if (c == '\\')
        {
            step(p);
            st = string_escape(p, &buf, &n, &cap);
            if (st != DLQ89_OK)
            {
                dlq89_priv_free(buf);
                return st;
            }
            continue;
        }
        if (c < 0x20)
        {
            dlq89_priv_free(buf);
            return syntax(p, "control-in-string", "unescaped control in string");
        }
        next = p->p;
        cp = 0;
        rc = unicode89_utf8_decode(p->s, p->n, p->p, &cp, &next);
        if (rc != UNICODE89_OK)
        {
            dlq89_priv_free(buf);
            return DLQ89_EUTF8;
        }
        while (p->p < next)
        {
            st = append_byte(&buf, &n, &cap, p->s[p->p]);
            if (st != DLQ89_OK)
            {
                dlq89_priv_free(buf);
                return st;
            }
            step(p);
        }
    }
    dlq89_priv_free(buf);
    return syntax(p, "unterminated-string", "unterminated string");
}

static dlq89_status integer_literal(dlq89_priv_parser *p, dlq89_priv_term *out)
{
    size_t start;
    size_t first;
    size_t digits;
    unsigned char *buf;
    size_t n;
    int negative;
    int at;

    negative = 0;
    if (p->s[p->p] == '-')
    {
        negative = 1;
        step(p);
    }
    start = p->p;
    while (p->p < p->n && is_digit(p->s[p->p]) != 0)
    {
        step(p);
    }
    if (p->p == start)
    {
        return syntax(p, "expected-integer", "expected integer digits");
    }
    first = start;
    while (first < p->p && p->s[first] == '0')
    {
        first = first + 1;
    }
    if (first == p->p)
    {
        negative = 0;
        first = p->p - 1;
    }
    digits = p->p - first;
    n = digits + (size_t)(negative != 0);
    buf = (unsigned char *)dlq89_priv_malloc(n);
    if (buf == NULL)
    {
        return DLQ89_ENOMEM;
    }
    at = 0;
    if (negative != 0)
    {
        buf[0] = '-';
        at = 1;
    }
    memcpy(buf + at, p->s + first, digits);
    out->is_var = 0;
    out->storage.data = buf;
    out->storage.size = n;
    out->value.kind = DLQ89_VALUE_INTEGER;
    out->value.data = buf;
    out->value.size = n;
    return DLQ89_OK;
}

static dlq89_status variable_index(dlq89_priv_query *q,
                                   dlq89_priv_bytes *name, size_t *out)
{
    size_t i;
    dlq89_priv_bytes *vars;

    for (i = 0; i < q->var_count; ++i)
    {
        if (bytes_equal(&q->vars[i], name) != 0)
        {
            dlq89_priv_free(name->data);
            *out = i;
            return DLQ89_OK;
        }
    }
    vars = (dlq89_priv_bytes *)dlq89_priv_realloc(
        q->vars, (q->var_count + 1) * sizeof(*vars));
    if (vars == NULL)
    {
        return DLQ89_ENOMEM;
    }
    q->vars = vars;
    q->vars[q->var_count] = *name;
    *out = q->var_count;
    q->var_count += 1;
    return DLQ89_OK;
}

static dlq89_status term(dlq89_priv_parser *p, dlq89_priv_query *q,
                         dlq89_priv_term *out)
{
    dlq89_priv_bytes name;
    dlq89_status st;

    memset(out, 0, sizeof(*out));
    skip_ws(p);
    if (p->p < p->n && p->s[p->p] == '"')
    {
        return string_literal(p, out);
    }
    if (p->p < p->n &&
        (is_digit(p->s[p->p]) != 0 || p->s[p->p] == '-'))
    {
        return integer_literal(p, out);
    }
    name.data = NULL;
    name.size = 0;
    st = identifier(p, &name);
    if (st != DLQ89_OK)
    {
        return st;
    }
    out->is_var = 1;
    st = variable_index(q, &name, &out->var);
    if (st != DLQ89_OK)
    {
        dlq89_priv_free(name.data);
    }
    return st;
}

static void atom_destroy(dlq89_priv_atom *a)
{
    size_t i;

    dlq89_priv_free(a->name.data);
    for (i = 0; i < a->arity; ++i)
    {
        dlq89_priv_free(a->terms[i].storage.data);
    }
    dlq89_priv_free(a->terms);
    memset(a, 0, sizeof(*a));
}

static dlq89_status atom(dlq89_priv_parser *p, dlq89_priv_query *q,
                         dlq89_priv_atom *out)
{
    dlq89_priv_term *terms;
    dlq89_priv_term t;
    dlq89_status st;

    memset(out, 0, sizeof(*out));
    skip_ws(p);
    st = identifier(p, &out->name);
    if (st != DLQ89_OK) return st;
    skip_ws(p);
    if (p->p >= p->n || p->s[p->p] != '(')
    {
        atom_destroy(out);
        return syntax(p, "expected-open-paren", "expected '('");
    }
    step(p);
    skip_ws(p);
    if (p->p < p->n && p->s[p->p] == ')')
    {
        step(p);
        return DLQ89_OK;
    }
    for (;;)
    {
        memset(&t, 0, sizeof(t));
        st = term(p, q, &t);
        if (st != DLQ89_OK)
        {
            atom_destroy(out);
            return st;
        }
        terms = (dlq89_priv_term *)dlq89_priv_realloc(
            out->terms, (out->arity + 1) * sizeof(*terms));
        if (terms == NULL)
        {
            dlq89_priv_free(t.storage.data);
            atom_destroy(out);
            return DLQ89_ENOMEM;
        }
        out->terms = terms;
        out->terms[out->arity] = t;
        out->arity += 1;
        skip_ws(p);
        if (p->p < p->n && p->s[p->p] == ')')
        {
            step(p);
            return DLQ89_OK;
        }
        if (p->p >= p->n || p->s[p->p] != ',')
        {
            atom_destroy(out);
            return syntax(p, "expected-comma-or-close", "expected ',' or ')'");
        }
        step(p);
        skip_ws(p);
    }
}

void dlq89_priv_query_destroy(dlq89_priv_query *q)
{
    size_t i;

    for (i = 0; i < q->var_count; ++i)
    {
        dlq89_priv_free(q->vars[i].data);
    }
    dlq89_priv_free(q->vars);
    for (i = 0; i < q->atom_count; ++i)
    {
        atom_destroy(&q->atoms[i]);
    }
    dlq89_priv_free(q->atoms);
    memset(q, 0, sizeof(*q));
}

static dlq89_status query(dlq89_priv_parser *p, dlq89_priv_query *q)
{
    dlq89_priv_atom a;
    dlq89_priv_atom *atoms;
    dlq89_status st;

    memset(q, 0, sizeof(*q));
    skip_ws(p);
    if (p->n - p->p < 2 || p->s[p->p] != '?' || p->s[p->p + 1] != '-')
    {
        return syntax(p, "expected-query", "expected '?-'");
    }
    step(p);
    step(p);
    for (;;)
    {
        memset(&a, 0, sizeof(a));
        st = atom(p, q, &a);
        if (st != DLQ89_OK)
        {
            dlq89_priv_query_destroy(q);
            return st;
        }
        atoms = (dlq89_priv_atom *)dlq89_priv_realloc(
            q->atoms, (q->atom_count + 1) * sizeof(*atoms));
        if (atoms == NULL)
        {
            atom_destroy(&a);
            dlq89_priv_query_destroy(q);
            return DLQ89_ENOMEM;
        }
        q->atoms = atoms;
        q->atoms[q->atom_count] = a;
        q->atom_count += 1;
        skip_ws(p);
        if (p->p < p->n && p->s[p->p] == '.')
        {
            step(p);
            return DLQ89_OK;
        }
        if (p->p >= p->n || p->s[p->p] != ',')
        {
            dlq89_priv_query_destroy(q);
            return syntax(p, "expected-query-separator", "expected ',' or '.'");
        }
        step(p);
    }
}

dlq89_status dlq89_priv_parse(dlq89_priv_parser *p, dlq89_document *document)
{
    dlq89_priv_query q;
    dlq89_priv_query *queries;
    dlq89_status st;

    skip_ws(p);
    while (p->p < p->n)
    {
        memset(&q, 0, sizeof(q));
        st = query(p, &q);
        if (st != DLQ89_OK)
        {
            return st;
        }
        queries = (dlq89_priv_query *)dlq89_priv_realloc(
            document->queries,
            (document->query_count + 1) * sizeof(*queries));
        if (queries == NULL)
        {
            dlq89_priv_query_destroy(&q);
            return DLQ89_ENOMEM;
        }
        document->queries = queries;
        document->queries[document->query_count] = q;
        document->query_count += 1;
        skip_ws(p);
    }
    return DLQ89_OK;
}
