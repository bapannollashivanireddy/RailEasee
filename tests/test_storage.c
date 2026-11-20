#include <stdio.h>
#include <string.h>

#include "storage.h"

/* Return a deterministic one-seat train fixture. O(1) time and space. */
static Train storage_test_train(void)
{
    Train train;
    memset(&train, 0, sizeof(train));
    train.number = 777;
    (void)snprintf(train.name, sizeof(train.name), "Storage Test");
    train.station_count = 2;
    (void)snprintf(train.stations[0], RAIL_MAX_NAME, "West");
    (void)snprintf(train.stations[1], RAIL_MAX_NAME, "East");
    train.class_mask = 1U << RAIL_CLASS_SL;
    train.coaches[RAIL_CLASS_SL] = 1;
    train.seats_per_coach[RAIL_CLASS_SL] = 1;
    train.rac_limit = 0;
    train.wl_limit = 2;
    return train;
}

/* Verify round-trip persistence, unique PNRs and corrupt-file rollback. O(B). */
int test_storage(void)
{
    SystemContext saved;
    SystemContext loaded;
    SystemContext corrupt;
    Train train = storage_test_train();
    Passenger passenger;
    char first_pnr[RAIL_PNR_LENGTH + 1];
    char second_pnr[RAIL_PNR_LENGTH + 1];
    char next_pnr[RAIL_PNR_LENGTH + 1];
    Booking booking;
    size_t position;
    RailError load_error;
    FILE *file;
    int ok = 0;
    const char *stage = "initialize";
    engine_init(&saved);
    engine_init(&loaded);
    engine_init(&corrupt);
    (void)snprintf(saved.notification_path, sizeof(saved.notification_path),
                   "tests/testdata/notifications.log");
    if (engine_add_train(&saved, &train) != RAIL_OK) {
        goto cleanup;
    }
    stage = "create bookings";
    memset(&passenger, 0, sizeof(passenger));
    (void)snprintf(passenger.name, sizeof(passenger.name), "Stored One");
    (void)snprintf(passenger.phone, sizeof(passenger.phone), "5550000001");
    passenger.age = 32;
    passenger.gender = 'F';
    stage = "first booking";
    if (engine_book(&saved, &passenger, train.number, "2032-04-05", "West",
                    "East", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, first_pnr) != RAIL_OK) {
        goto cleanup;
    }
    (void)snprintf(passenger.name, sizeof(passenger.name), "Stored Two");
    (void)snprintf(passenger.phone, sizeof(passenger.phone), "5550000002");
    stage = "second booking";
    if (engine_book(&saved, &passenger, train.number, "2032-04-05", "West",
                    "East", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, second_pnr) != RAIL_OK) {
        goto cleanup;
    }
    stage = "cancel booking";
    if (engine_cancel(&saved, first_pnr, NULL) != RAIL_OK) {
        goto cleanup;
    }
    stage = "save files";
    if (storage_save(&saved, "tests/testdata") != RAIL_OK) {
        goto cleanup;
    }
    stage = "load files";
    load_error = storage_load(&loaded, "tests/testdata");
    if (load_error != RAIL_OK) {
        (void)fprintf(stderr, "Storage load returned %s.\n",
                      rail_error_message(load_error));
        goto cleanup;
    }
    stage = "verify loaded booking";
    if (engine_status(&loaded, second_pnr, &booking, &position) != RAIL_OK ||
        booking.status != RAIL_STATUS_CNF ||
        booking.initial_status != RAIL_STATUS_WL ||
        booking.original_position != 1 ||
        avl_find_train(loaded.train_root, train.number) == NULL) {
        goto cleanup;
    }
    (void)snprintf(passenger.name, sizeof(passenger.name), "Stored Three");
    (void)snprintf(passenger.phone, sizeof(passenger.phone), "5550000003");
    if (engine_book(&loaded, &passenger, train.number, "2032-04-05", "West",
                    "East", RAIL_CLASS_SL, RAIL_QUOTA_GENERAL, next_pnr) != RAIL_OK ||
        strcmp(next_pnr, first_pnr) == 0 || strcmp(next_pnr, second_pnr) == 0) {
        goto cleanup;
    }
    stage = "advance persisted PNR counter";
    file = fopen("tests/testdata/bookings.dat", "ab");
    if (file == NULL) {
        goto cleanup;
    }
    stage = "reject corrupt file";
    if (fputc('x', file) == EOF) {
        (void)fclose(file);
        goto cleanup;
    }
    if (fclose(file) != 0 ||
        storage_load(&corrupt, "tests/testdata") != RAIL_ERR_CORRUPT ||
        corrupt.train_root != NULL || corrupt.booking_count != 0) {
        goto cleanup;
    }
    ok = 1;
cleanup:
    if (!ok) {
        (void)fprintf(stderr, "Storage test failed at %s.\n", stage);
    }
    engine_destroy(&saved);
    engine_destroy(&loaded);
    engine_destroy(&corrupt);
    (void)remove("tests/testdata/trains.dat");
    (void)remove("tests/testdata/bookings.dat");
    (void)remove("tests/testdata/notifications.log");
    return ok;
}
