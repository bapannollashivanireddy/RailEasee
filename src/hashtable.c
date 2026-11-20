#include <string.h>
#include <stdlib.h>

#include "hashtable.h"

/* Hash PNR bytes with FNV-1a. O(PNR length) time and O(1) space. */
static size_t hash_pnr(const char *pnr, size_t bucket_count)
{
    uint64_t value = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0; pnr[i] != '\0'; ++i) {
        value = (value ^ (unsigned char)pnr[i]) * UINT64_C(1099511628211);
    }
    return (size_t)(value % bucket_count);
}

/* Resize and rechain every record. Params: context/table size. Returns error.
 * O(n+m) time and O(m) temporary space for n records and m buckets. */
static RailError resize_table(SystemContext *context, size_t bucket_count)
{
    BookingRecord **buckets;
    size_t i;
    if (bucket_count == 0 || bucket_count > SIZE_MAX / sizeof(*buckets)) {
        return RAIL_ERR_FULL;
    }
    buckets = calloc(bucket_count, sizeof(*buckets));
    if (buckets == NULL) {
        return RAIL_ERR_MEMORY;
    }
    for (i = 0; i < context->booking_bucket_count; ++i) {
        BookingRecord *record = context->booking_buckets[i];
        while (record != NULL) {
            BookingRecord *next = record->hash_next;
            size_t bucket = hash_pnr(record->booking.pnr, bucket_count);
            record->hash_next = buckets[bucket];
            buckets[bucket] = record;
            record = next;
        }
    }
    free(context->booking_buckets);
    context->booking_buckets = buckets;
    context->booking_bucket_count = bucket_count;
    return RAIL_OK;
}

/* Find a PNR record. Average O(1), worst O(n), O(1) space. */
BookingRecord *booking_hash_find(const SystemContext *context, const char *pnr)
{
    BookingRecord *record;
    if (context == NULL || pnr == NULL || context->booking_bucket_count == 0 ||
        context->booking_buckets == NULL) {
        return NULL;
    }
    record = context->booking_buckets[
        hash_pnr(pnr, context->booking_bucket_count)];
    while (record != NULL && strcmp(record->booking.pnr, pnr) != 0) {
        record = record->hash_next;
    }
    return record;
}

/* Insert a unique PNR. Amortized average O(1), worst O(n) time and O(n) resize space. */
RailError booking_hash_insert(SystemContext *context, BookingRecord *record)
{
    size_t bucket;
    RailError error;
    if (context == NULL || record == NULL || record->booking.pnr[0] == '\0') {
        return RAIL_ERR_INVALID;
    }
    if (booking_hash_find(context, record->booking.pnr) != NULL) {
        return RAIL_ERR_DUPLICATE;
    }
    if (context->booking_count == SIZE_MAX) {
        return RAIL_ERR_FULL;
    }
    if (context->booking_bucket_count == 0) {
        error = resize_table(context, RAIL_HASH_BUCKETS);
        if (error != RAIL_OK) {
            return error;
        }
    }
    if (context->booking_count + 1 >
        context->booking_bucket_count - context->booking_bucket_count / 4) {
        if (context->booking_bucket_count > (SIZE_MAX - 1) / 2) {
            return RAIL_ERR_FULL;
        }
        error = resize_table(context, context->booking_bucket_count * 2 + 1);
        if (error != RAIL_OK) {
            return error;
        }
    }
    bucket = hash_pnr(record->booking.pnr, context->booking_bucket_count);
    record->hash_next = context->booking_buckets[bucket];
    context->booking_buckets[bucket] = record;
    ++context->booking_count;
    return RAIL_OK;
}

/* Detach and return a PNR record. Average O(1), worst O(n), O(1) space. */
BookingRecord *booking_hash_remove(SystemContext *context, const char *pnr)
{
    size_t bucket;
    BookingRecord **link;
    if (context == NULL || pnr == NULL) {
        return NULL;
    }
    if (context->booking_bucket_count == 0 || context->booking_buckets == NULL) {
        return NULL;
    }
    bucket = hash_pnr(pnr, context->booking_bucket_count);
    link = &context->booking_buckets[bucket];
    while (*link != NULL && strcmp((*link)->booking.pnr, pnr) != 0) {
        link = &(*link)->hash_next;
    }
    if (*link == NULL) {
        return NULL;
    }
    {
        BookingRecord *removed = *link;
        *link = removed->hash_next;
        removed->hash_next = NULL;
        --context->booking_count;
        return removed;
    }
}

/* Destroy all hash records. O(n) time and O(1) auxiliary space. */
void booking_hash_destroy(SystemContext *context)
{
    size_t i;
    if (context == NULL) {
        return;
    }
    for (i = 0; i < context->booking_bucket_count; ++i) {
        BookingRecord *record = context->booking_buckets[i];
        while (record != NULL) {
            BookingRecord *next = record->hash_next;
            free(record);
            record = next;
        }
        context->booking_buckets[i] = NULL;
    }
    free(context->booking_buckets);
    context->booking_buckets = NULL;
    context->booking_bucket_count = 0;
    context->booking_count = 0;
}
