#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hashtable.h"

/* Exercise lookup, deletion and growth. O(n) test setup. */
int test_hashtable(void)
{
    SystemContext context;
    BookingRecord first;
    BookingRecord second;
    BookingRecord **records;
    size_t i;
    size_t inserted = 0;
    int passed = 1;
    memset(&context, 0, sizeof(context));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    (void)snprintf(first.booking.pnr, sizeof(first.booking.pnr), "1234567890");
    (void)snprintf(second.booking.pnr, sizeof(second.booking.pnr), "1234567891");
    if (booking_hash_insert(&context, &first) != RAIL_OK ||
        booking_hash_insert(&context, &second) != RAIL_OK ||
        booking_hash_find(&context, first.booking.pnr) != &first ||
        booking_hash_insert(&context, &first) != RAIL_ERR_DUPLICATE) {
        passed = 0;
    }
    if (booking_hash_remove(&context, first.booking.pnr) != &first ||
        booking_hash_find(&context, first.booking.pnr) != NULL ||
        booking_hash_remove(&context, "0000000000") != NULL) {
        passed = 0;
    }
    (void)booking_hash_remove(&context, second.booking.pnr);
    if (context.booking_count != 0) {
        passed = 0;
    }
    booking_hash_destroy(&context);
    if (!passed) {
        return 0;
    }
    records = calloc(1100, sizeof(*records));
    if (records == NULL) {
        return 0;
    }
    for (i = 0; i < 1100; ++i) {
        records[i] = calloc(1, sizeof(*records[i]));
        if (records[i] == NULL) {
            passed = 0;
            break;
        }
        (void)snprintf(records[i]->booking.pnr,
                       sizeof(records[i]->booking.pnr), "%010u",
                       (unsigned)(1000000000U + i));
        if (booking_hash_insert(&context, records[i]) != RAIL_OK) {
            free(records[i]);
            records[i] = NULL;
            passed = 0;
            break;
        }
        ++inserted;
    }
    if (inserted != 1100 || context.booking_bucket_count <= RAIL_HASH_BUCKETS ||
        booking_hash_find(&context, records[1099]->booking.pnr) != records[1099]) {
        passed = 0;
    }
    booking_hash_destroy(&context);
    for (i = inserted; i < 1100; ++i) {
        free(records[i]);
    }
    free(records);
    return passed != 0;
}
