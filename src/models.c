#include "models.h"

/* Map a class enum to its display name. O(1) time and O(1) space. */
const char *rail_class_name(RailClass value)
{
    static const char *names[] = {"SL", "3A", "2A", "1A"};
    return value >= RAIL_CLASS_SL && value < RAIL_CLASS_COUNT ?
        names[value] : "INVALID";
}

/* Map a booking status to its display name. O(1) time and O(1) space. */
const char *rail_status_name(BookingStatus value)
{
    static const char *names[] = {"CNF", "RAC", "WL"};
    return value >= RAIL_STATUS_CNF && value <= RAIL_STATUS_WL ?
        names[value] : "INVALID";
}

/* Map a quota enum to its display name. O(1) time and O(1) space. */
const char *rail_quota_name(QuotaType value)
{
    static const char *names[] = {"GENERAL", "TATKAL", "LADIES", "SENIOR"};
    return value >= RAIL_QUOTA_GENERAL && value <= RAIL_QUOTA_SENIOR ?
        names[value] : "INVALID";
}

/* Describe a public error code. O(1) time and O(1) space. */
const char *rail_error_message(RailError error)
{
    static const char *messages[] = {
        "success", "invalid input", "not found", "capacity full",
        "active bookings prevent this change", "out of memory",
        "duplicate record", "file I/O error",
        "corrupt data"
    };
    return error >= RAIL_OK && error <= RAIL_ERR_CORRUPT ?
        messages[error] : "unknown error";
}
