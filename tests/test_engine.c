#include <stdio.h>
#include <string.h>

#include "engine.h"
#include "probability.h"

/* Build a small test route. O(1) time and space. */
static Train make_train(unsigned seats, unsigned rac, unsigned waiting)
{
    Train train;
    memset(&train, 0, sizeof(train));
    train.number = 9001;
    (void)snprintf(train.name, sizeof(train.name), "Test Express");
    train.station_count = 3;
    (void)snprintf(train.stations[0], RAIL_MAX_NAME, "A");
    (void)snprintf(train.stations[1], RAIL_MAX_NAME, "B");
    (void)snprintf(train.stations[2], RAIL_MAX_NAME, "C");
    train.class_mask = 1U << RAIL_CLASS_SL;
    train.coaches[RAIL_CLASS_SL] = 1;
    train.seats_per_coach[RAIL_CLASS_SL] = seats;
    train.rac_limit = rac;
    train.wl_limit = waiting;
    return train;
}

/* Build a unique test passenger. O(1) time and space. */
static Passenger make_passenger(unsigned index)
{
    Passenger passenger;
    memset(&passenger, 0, sizeof(passenger));
    (void)snprintf(passenger.name, sizeof(passenger.name), "Passenger %u", index);
    (void)snprintf(passenger.phone, sizeof(passenger.phone), "%010u", index + 1);
    passenger.age = 30;
    passenger.gender = 'M';
    return passenger;
}

static int stress_book_and_cancel(void);
static int test_segment_reuse(void);
static int test_waiting_segment_eligibility(void);
static int test_probability_estimate(void);

/* Assert exact promotion order, then run 10,000 mixed operations. O(n^2) setup. */
int test_engine(void)
{
    SystemContext context;
    Train train = make_train(5, 10, 20);
    char pnrs[35][RAIL_PNR_LENGTH + 1];
    Booking booking;
    BookingRecord *record;
    Trip *trip;
    size_t position;
    unsigned i;
    bool ok = true;
    engine_init(&context);
    (void)snprintf(context.notification_path, sizeof(context.notification_path),
                   "tests/testdata/notifications.log");
    if (engine_add_train(&context, &train) != RAIL_OK) {
        engine_destroy(&context);
        return 0;
    }
    (void)snprintf(context.notification_path, sizeof(context.notification_path),
                   "data/notifications.log");
    for (i = 0; i < 35; ++i) {
        Passenger passenger = make_passenger(i);
        QuotaType quota = i >= 15 && i < 20 ? RAIL_QUOTA_GENERAL :
            (QuotaType)(i % RAIL_QUOTA_COUNT);
        RailError error = engine_book(&context, &passenger, train.number,
                                      "2030-01-02", "A", "C", RAIL_CLASS_SL,
                                      quota, pnrs[i]);
        if (error != RAIL_OK) {
            ok = false;
            break;
        }
    }
    for (i = 0; ok && i < 5; ++i) {
        if (engine_cancel(&context, pnrs[i], NULL) != RAIL_OK) {
            ok = false;
        }
    }
    for (i = 5; ok && i < 10; ++i) {
        if (engine_status(&context, pnrs[i], &booking, &position) != RAIL_OK ||
            booking.status != RAIL_STATUS_CNF) {
            ok = false;
        }
    }
    trip = engine_find_trip(&context, train.number, "2030-01-02", RAIL_CLASS_SL);
    if (trip == NULL || trip->rac.size != 10 || trip->waiting.size != 15) {
        ok = false;
    }
    if (ok) {
        for (i = 0; i < 5; ++i) {
            size_t index = (trip->rac.head + i) % trip->rac.capacity;
            if (strcmp(trip->rac.items[index]->booking.pnr, pnrs[10 + i]) != 0) {
                ok = false;
            }
        }
        for (i = 0; i < 5; ++i) {
            size_t index = (trip->rac.head + 5 + i) % trip->rac.capacity;
            if (strcmp(trip->rac.items[index]->booking.pnr, pnrs[15 + i]) != 0) {
                ok = false;
            }
        }
        record = heap_peek(&trip->waiting);
        if (record == NULL || strcmp(record->booking.pnr, pnrs[20]) != 0) {
            ok = false;
        }
    }
    engine_destroy(&context);
    if (!ok) {
        (void)remove("tests/testdata/notifications.log");
        return 0;
    }
    if (!test_segment_reuse()) {
        return 0;
    }
    if (!test_waiting_segment_eligibility()) {
        return 0;
    }
    if (!test_probability_estimate()) {
        return 0;
    }
    return stress_book_and_cancel();
}

/* Verify a seat is reusable only when every requested segment is free. */
static int test_segment_reuse(void)
{
    SystemContext context;
    Train train = make_train(1, 1, 1);
    Passenger passengers[3];
    char pnrs[3][RAIL_PNR_LENGTH + 1];
    Booking booking;
    size_t position;
    unsigned i;
    int ok = 0;
    engine_init(&context);
    (void)snprintf(context.notification_path, sizeof(context.notification_path),
                   "tests/testdata/notifications.log");
    if (engine_add_train(&context, &train) != RAIL_OK) {
        engine_destroy(&context);
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        passengers[i] = make_passenger(i + 100);
    }
    if (engine_book(&context, &passengers[0], train.number, "2033-01-02", "A",
                    "B", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnrs[0]) != RAIL_OK ||
        engine_book(&context, &passengers[1], train.number, "2033-01-02", "B",
                    "C", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnrs[1]) != RAIL_OK ||
        engine_book(&context, &passengers[2], train.number, "2033-01-02", "A",
                    "C", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnrs[2]) != RAIL_OK ||
        engine_cancel(&context, pnrs[0], NULL) != RAIL_OK ||
        engine_status(&context, pnrs[2], &booking, &position) != RAIL_OK ||
        booking.status != RAIL_STATUS_RAC ||
        engine_cancel(&context, pnrs[1], NULL) != RAIL_OK ||
        engine_status(&context, pnrs[2], &booking, &position) != RAIL_OK ||
        booking.status != RAIL_STATUS_CNF || booking.seat_number != 1) {
        goto cleanup;
    }
    ok = 1;
cleanup:
    engine_destroy(&context);
    (void)remove("tests/testdata/notifications.log");
    return ok;
}

/* Select the highest-priority WL ticket eligible for a released segment. */
static int test_waiting_segment_eligibility(void)
{
    SystemContext context;
    Train train = make_train(1, 0, 2);
    Passenger passengers[4];
    char pnrs[4][RAIL_PNR_LENGTH + 1];
    Booking booking;
    size_t position;
    unsigned i;
    int ok = 0;
    engine_init(&context);
    (void)snprintf(context.notification_path, sizeof(context.notification_path),
                   "tests/testdata/notifications.log");
    if (engine_add_train(&context, &train) != RAIL_OK) {
        engine_destroy(&context);
        return 0;
    }
    for (i = 0; i < 4; ++i) {
        passengers[i] = make_passenger(i + 200);
    }
    if (engine_book(&context, &passengers[0], train.number, "2033-02-02", "A",
                    "B", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnrs[0]) != RAIL_OK ||
        engine_book(&context, &passengers[1], train.number, "2033-02-02", "B",
                    "C", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnrs[1]) != RAIL_OK ||
        engine_book(&context, &passengers[2], train.number, "2033-02-02", "A",
                    "C", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnrs[2]) != RAIL_OK ||
        engine_book(&context, &passengers[3], train.number, "2033-02-02", "A",
                    "B", RAIL_CLASS_SL, RAIL_QUOTA_TATKAL, pnrs[3]) != RAIL_OK ||
        engine_cancel(&context, pnrs[0], NULL) != RAIL_OK ||
        engine_status(&context, pnrs[2], &booking, &position) != RAIL_OK ||
        booking.status != RAIL_STATUS_WL ||
        engine_status(&context, pnrs[3], &booking, &position) != RAIL_OK ||
        booking.status != RAIL_STATUS_CNF || booking.seat_number != 1) {
        goto cleanup;
    }
    ok = 1;
cleanup:
    engine_destroy(&context);
    (void)remove("tests/testdata/notifications.log");
    return ok;
}

/* Verify the per-train/class probability formula and label. O(B+S). */
static int test_probability_estimate(void)
{
    SystemContext context;
    Train train = make_train(1, 0, 1);
    Passenger passengers[2];
    Booking booking;
    char pnr[RAIL_PNR_LENGTH + 1];
    size_t position;
    Trip *trip;
    ConfirmationEstimate estimate;
    int ok = 0;
    train.cancellation_rates[RAIL_CLASS_SL] = 1.0;
    engine_init(&context);
    (void)snprintf(context.notification_path, sizeof(context.notification_path),
                   "tests/testdata/notifications.log");
    if (engine_add_train(&context, &train) != RAIL_OK) {
        engine_destroy(&context);
        return 0;
    }
    passengers[0] = make_passenger(300);
    passengers[1] = make_passenger(301);
    if (engine_book(&context, &passengers[0], train.number, "2034-01-02", "A",
                    "C", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnr) != RAIL_OK ||
        engine_book(&context, &passengers[1], train.number, "2034-01-02", "A",
                    "C", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, pnr) != RAIL_OK ||
        engine_status(&context, pnr, &booking, &position) != RAIL_OK) {
        engine_destroy(&context);
        return 0;
    }
    booking.queue_position = position;
    trip = engine_find_trip(&context, train.number, "2034-01-02", RAIL_CLASS_SL);
    estimate = probability_estimate(&context, trip, &booking, 30);
    ok = estimate.percentage > 49.9 && estimate.percentage < 50.1 &&
        strcmp(estimate.label, "Medium") == 0;
    engine_destroy(&context);
    (void)remove("tests/testdata/notifications.log");
    return ok;
}

/* Advance a local deterministic PRNG. O(1) time and O(1) space. */
static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

/* Run 5,000 booking/cancellation pairs. O(n) time for bounded active state. */
static int stress_book_and_cancel(void)
{
    SystemContext context;
    Train train = make_train(1, 0, 0);
    uint32_t state = UINT32_C(0x12345678);
    unsigned i;
    int ok = 1;
    engine_init(&context);
    (void)snprintf(context.notification_path, sizeof(context.notification_path),
                   "tests/testdata/notifications.log");
    if (engine_add_train(&context, &train) != RAIL_OK) {
        engine_destroy(&context);
        return 0;
    }
    for (i = 0; i < 5000; ++i) {
        Passenger passenger = make_passenger(next_random(&state));
        char pnr[RAIL_PNR_LENGTH + 1];
        if (engine_book(&context, &passenger, train.number, "2031-02-03", "A",
                        "C", RAIL_CLASS_SL, (QuotaType)(state % RAIL_QUOTA_COUNT),
                        pnr) != RAIL_OK ||
            engine_cancel(&context, pnr, NULL) != RAIL_OK) {
            ok = 0;
            break;
        }
    }
    if (context.booking_count != 0) {
        ok = 0;
    }
    engine_destroy(&context);
    (void)remove("tests/testdata/notifications.log");
    return ok;
}
