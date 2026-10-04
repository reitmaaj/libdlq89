#include <dlq89.h>

#include <string.h>

static int reject(const char *s, dlq89_status expected)
{
    dlq89_document *doc;
    dlq89_diagnostic d;
    dlq89_status st;

    doc = NULL;
    st = dlq89_parse((const unsigned char *)s, strlen(s), &doc, &d);
    if (doc != NULL)
    {
        dlq89_document_destroy(doc);
        return 0;
    }
    return st == expected;
}

static int accept(const char *s)
{
    dlq89_document *doc;
    dlq89_diagnostic d;
    dlq89_status st;
    int ok;

    doc = NULL;
    st = dlq89_parse((const unsigned char *)s, strlen(s), &doc, &d);
    ok = st == DLQ89_OK && doc != NULL;
    dlq89_document_destroy(doc);
    return ok;
}

int main(void)
{
    dlq89_document *doc;
    dlq89_diagnostic d;
    static const unsigned char bad_utf8[] = {
        '?', '-', ' ', 'p', '(', 0xff, ')', '.'
    };

    if (!accept("?- p(1)."))
    {
        return 1;
    }
    if (!accept("?- p(-42), q(0), r(007)."))
    {
        return 1;
    }
    if (!reject("?- p(-).", DLQ89_ESYNTAX))
    {
        return 1;
    }
    if (!reject("?- p(_X).", DLQ89_ESYNTAX))
    {
        return 2;
    }
    if (!reject("?- p(X). /* no comments */", DLQ89_ESYNTAX))
    {
        return 3;
    }
    if (!reject("?- p(\"\\uD800\").", DLQ89_ESYNTAX))
    {
        return 4;
    }
    if (!reject("?- p(\"\\uDC00\").", DLQ89_ESYNTAX))
    {
        return 5;
    }
    doc = NULL;
    if (dlq89_parse(bad_utf8, sizeof(bad_utf8), &doc, &d) != DLQ89_EUTF8)
    {
        return 6;
    }
    if (dlq89_parse((const unsigned char *)"?- p(\"\\uD834\\uDD1E\").",
                    strlen("?- p(\"\\uD834\\uDD1E\")."), &doc,
                    &d) != DLQ89_OK)
    {
        return 7;
    }
    dlq89_document_destroy(doc);
    return 0;
}
