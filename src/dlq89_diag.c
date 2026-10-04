#include "dlq89_priv.h"

void dlq89_priv_diag(dlq89_diagnostic *diagnostic, dlq89_status status,
                     size_t offset, size_t end_offset, size_t line,
                     size_t column, int database_status, const char *code,
                     const char *message)
{
    if (diagnostic == NULL)
    {
        return;
    }
    diagnostic->status = status;
    diagnostic->offset = offset;
    diagnostic->end_offset = end_offset;
    diagnostic->line = line;
    diagnostic->column = column;
    diagnostic->database_status = database_status;
    diagnostic->code = code;
    diagnostic->message = message;
}
