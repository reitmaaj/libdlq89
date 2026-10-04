#include <dlq89.h>

const char *dlq89_status_name(dlq89_status status)
{
    switch (status)
    {
    case DLQ89_OK: return "OK";
    case DLQ89_END: return "END";
    case DLQ89_EINVAL: return "EINVAL";
    case DLQ89_ENOMEM: return "ENOMEM";
    case DLQ89_EUTF8: return "EUTF8";
    case DLQ89_ESYNTAX: return "ESYNTAX";
    case DLQ89_EQUERY: return "EQUERY";
    case DLQ89_EDATABASE: return "EDATABASE";
    case DLQ89_EINTERNAL: return "EINTERNAL";
    default: return "UNKNOWN";
    }
}

const char *dlq89_status_message(dlq89_status status)
{
    switch (status)
    {
    case DLQ89_OK: return "success";
    case DLQ89_END: return "end of iteration";
    case DLQ89_EINVAL: return "invalid argument";
    case DLQ89_ENOMEM: return "allocation failed";
    case DLQ89_EUTF8: return "invalid UTF-8";
    case DLQ89_ESYNTAX: return "invalid DLQ syntax";
    case DLQ89_EQUERY: return "invalid query against database schema";
    case DLQ89_EDATABASE: return "database operation failed";
    case DLQ89_EINTERNAL: return "internal invariant failed";
    default: return "unknown status";
    }
}
