#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"

typedef struct {
    const char *source;
    const char *destination;
    const char *date;
    size_t matches;
} SearchState;

/* Read one bounded, nonempty line. O(capacity) time and O(1) space. */
static bool read_text(const char *prompt, char *buffer, size_t capacity)
{
    size_t length;
    int ch;
    (void)printf("%s", prompt);
    if (fgets(buffer, (int)capacity, stdin) == NULL) {
        return false;
    }
    length = strlen(buffer);
    if (length == 0 || buffer[length - 1] != '\n') {
        while ((ch = getchar()) != '\n' && ch != EOF) {
        }
        (void)fprintf(stderr, "Input is too long.\n");
        return false;
    }
    buffer[--length] = '\0';
    if (length > 0 && buffer[length - 1] == '\r') {
        buffer[length - 1] = '\0';
        --length;
    }
    if (length == 0) {
        (void)fprintf(stderr, "Input cannot be empty.\n");
        return false;
    }
    return true;
}

/* Parse a bounded unsigned integer. O(input length) time and O(1) space. */
static bool read_number(const char *prompt, unsigned minimum, unsigned maximum,
                        unsigned *value)
{
    char line[64];
    char *end;
    unsigned long parsed;
    if (!read_text(prompt, line, sizeof(line))) {
        return false;
    }
    errno = 0;
    parsed = strtoul(line, &end, 10);
    if (errno != 0 || end == line || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        (void)fprintf(stderr, "Enter a whole number from %u to %u.\n",
                      minimum, maximum);
        return false;
    }
    *value = (unsigned)parsed;
    return true;
}

/* Compare user-visible names case-insensitively. O(n) time and O(1) space. */
static bool equal_text(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

/* Check that destination follows source. O(stations) time and O(1) space. */
static bool route_matches(const Train *train, const char *source,
                          const char *destination)
{
    size_t i;
    bool found_source = false;
    for (i = 0; i < train->station_count; ++i) {
        if (!found_source && equal_text(train->stations[i], source)) {
            found_source = true;
        } else if (found_source && equal_text(train->stations[i], destination)) {
            return true;
        }
    }
    return false;
}

/* Print a matching train row. O(stations+classes) time and O(1) space. */
static void print_train_row(const Train *train, void *user_data)
{
    SearchState *state = user_data;
    size_t i;
    if (!route_matches(train, state->source, state->destination)) {
        return;
    }
    ++state->matches;
    (void)printf("%-8u %-24s %-16s %-16s ", train->number, train->name,
                 state->source, state->destination);
    for (i = 0; i < RAIL_CLASS_COUNT; ++i) {
        if ((train->class_mask & (1U << i)) != 0) {
            (void)printf("%s:%u  ", rail_class_name((RailClass)i),
                         train->coaches[i] * train->seats_per_coach[i]);
        }
    }
    (void)printf(" %s\n", state->date);
}

/* Search directory routes for a requested journey. O(T) time. */
static void search_trains(SystemContext *context)
{
    char source[RAIL_MAX_NAME];
    char destination[RAIL_MAX_NAME];
    char date[RAIL_DATE_LENGTH + 2];
    SearchState state;
    if (!read_text("Source station: ", source, sizeof(source)) ||
        !read_text("Destination station: ", destination, sizeof(destination)) ||
        !read_text("Journey date (YYYY-MM-DD): ", date, sizeof(date))) {
        return;
    }
    if (!engine_valid_date(date)) {
        (void)fprintf(stderr, "Enter a valid calendar date as YYYY-MM-DD.\n");
        return;
    }
    state.source = source;
    state.destination = destination;
    state.date = date;
    state.matches = 0;
    (void)printf("\n%-8s %-24s %-16s %-16s %s\n",
                 "Train", "Name", "From", "To", "Classes / date");
    avl_visit_trains(context->train_root, print_train_row, &state);
    if (state.matches == 0) {
        (void)printf("No matching trains.\n");
    }
}

/* Read a class enum in range. O(1) time and O(1) space. */
static bool read_class(RailClass *travel_class)
{
    unsigned value;
    (void)printf("Class: 0=SL 1=3A 2=2A 3=1A\n");
    if (!read_number("Choose class: ", 0, RAIL_CLASS_COUNT - 1, &value)) {
        return false;
    }
    *travel_class = (RailClass)value;
    return true;
}

/* Add a passenger booking from validated CLI fields. O(engine booking). */
static void book_ticket(SystemContext *context)
{
    Passenger passenger;
    unsigned train_number;
    unsigned age;
    unsigned quota;
    char date[RAIL_DATE_LENGTH + 2];
    char source[RAIL_MAX_NAME];
    char destination[RAIL_MAX_NAME];
    char gender[8];
    char pnr[RAIL_PNR_LENGTH + 3];
    RailClass travel_class;
    Trip *trip;
    RailError error;
    memset(&passenger, 0, sizeof(passenger));
    if (!read_text("Passenger name: ", passenger.name, sizeof(passenger.name)) ||
        !read_number("Age (1-120): ", 1, 120, &age) ||
        !read_text("Gender (M/F/O): ", gender, sizeof(gender)) ||
        !read_text("Phone: ", passenger.phone, sizeof(passenger.phone)) ||
        !read_text("Berth preference (lower/middle/upper/any): ",
                   passenger.berth_preference, sizeof(passenger.berth_preference)) ||
        !read_number("Train number: ", 1, UINT_MAX, &train_number) ||
        !read_text("Journey date (YYYY-MM-DD): ", date, sizeof(date)) ||
        !read_text("Source: ", source, sizeof(source)) ||
        !read_text("Destination: ", destination, sizeof(destination)) ||
        !read_class(&travel_class) ||
        !read_number("Quota: 0=GENERAL 1=TATKAL 2=LADIES 3=SENIOR: ",
                     0, RAIL_QUOTA_SENIOR, &quota)) {
        return;
    }
    if (strlen(gender) != 1 ||
        (toupper((unsigned char)gender[0]) != 'M' &&
         toupper((unsigned char)gender[0]) != 'F' &&
         toupper((unsigned char)gender[0]) != 'O')) {
        (void)fprintf(stderr, "Gender must be M, F, or O.\n");
        return;
    }
    passenger.age = age;
    passenger.gender = (char)toupper((unsigned char)gender[0]);
    trip = engine_find_trip(context, train_number, date, travel_class);
    (void)printf("Before booking:\n");
    engine_print_queues(trip);
    error = engine_book(context, &passenger, train_number, date, source,
                        destination, travel_class, (QuotaType)quota, pnr);
    if (error == RAIL_OK) {
        Booking booking;
        size_t position;
        if (engine_status(context, pnr, &booking, &position) == RAIL_OK) {
            (void)printf("Booked PNR %s: %s", pnr,
                         rail_status_name(booking.status));
            if (booking.status == RAIL_STATUS_CNF) {
                (void)printf(" seat %d", booking.seat_number);
            } else {
                (void)printf(" position %u", (unsigned)position);
            }
            (void)printf("\n");
            trip = engine_find_trip(context, booking.train_number,
                                    booking.journey_date, booking.travel_class);
            (void)printf("After booking:\n");
            engine_print_queues(trip);
        }
    } else {
        (void)fprintf(stderr, "Booking failed: %s\n", rail_error_message(error));
    }
}

/* Cancel a ticket and display its refund estimate. O(engine cancellation). */
static void cancel_ticket(SystemContext *context)
{
    char pnr[RAIL_PNR_LENGTH + 3];
    Booking booking;
    size_t position;
    double refund;
    RailError error;
    Trip *trip;
    if (!read_text("PNR: ", pnr, sizeof(pnr))) {
        return;
    }
    if (engine_status(context, pnr, &booking, &position) != RAIL_OK) {
        (void)printf("Cancellation failed: %s\n",
                     rail_error_message(RAIL_ERR_NOT_FOUND));
        return;
    }
    trip = engine_find_trip(context, booking.train_number,
                            booking.journey_date, booking.travel_class);
    (void)printf("Before cancellation:\n");
    engine_print_queues(trip);
    error = engine_cancel(context, pnr, &refund);
    if (error == RAIL_OK) {
        (void)printf("Cancelled. Estimated refund: Rs. %.2f\n", refund);
        (void)printf("After cancellation:\n");
        engine_print_queues(engine_find_trip(context, booking.train_number,
                                             booking.journey_date,
                                             booking.travel_class));
    } else {
        (void)fprintf(stderr, "Cancellation failed: %s\n", rail_error_message(error));
    }
}

/* Look up a ticket and show its live queue position. O(1) avg + O(W). */
static void show_pnr_status(SystemContext *context)
{
    char pnr[RAIL_PNR_LENGTH + 3];
    Booking booking;
    size_t position;
    if (!read_text("PNR: ", pnr, sizeof(pnr))) {
        return;
    }
    if (engine_status(context, pnr, &booking, &position) != RAIL_OK) {
        (void)printf("PNR not found.\n");
        return;
    }
    (void)printf("PNR %s | %s | Train %u | %s %s -> %s | %s",
                 booking.pnr, rail_status_name(booking.status), booking.train_number,
                 booking.journey_date, booking.source, booking.destination,
                 rail_class_name(booking.travel_class));
    if (booking.status == RAIL_STATUS_CNF) {
        (void)printf(" | Seat %d\n", booking.seat_number);
    } else {
        (void)printf(" | Current position %u", (unsigned)position);
        if (booking.original_position != 0 &&
            booking.original_position != position) {
            (void)printf(" (booked at %s %u)",
                     rail_status_name(booking.initial_status),
                         booking.original_position);
        }
        (void)printf("\n");
    }
}

/* Estimate a passenger's confirmation probability. O(B+S) time. */
static void show_probability(SystemContext *context)
{
    char pnr[RAIL_PNR_LENGTH + 3];
    unsigned days;
    Booking booking;
    Booking live;
    size_t position;
    Trip *trip;
    ConfirmationEstimate estimate;
    if (!read_text("PNR: ", pnr, sizeof(pnr)) ||
        !read_number("Days to departure: ", 0, 3650, &days)) {
        return;
    }
    if (engine_status(context, pnr, &booking, &position) != RAIL_OK ||
        booking.status == RAIL_STATUS_CNF) {
        (void)printf("A valid RAC/WL PNR is required.\n");
        return;
    }
    live = booking;
    live.queue_position = position;
    trip = engine_find_trip(context, booking.train_number,
                            booking.journey_date, booking.travel_class);
    estimate = probability_estimate(context, trip, &live, days);
    (void)printf("Estimated confirmation chance: %.1f%% (%s)\n",
                 estimate.percentage, estimate.label);
}

typedef struct {
    SystemContext *context;
    unsigned train_number;
    const char *date;
    RailClass travel_class;
    size_t confirmed;
    size_t rac;
    size_t waiting;
} OccupancyState;

/* Count selected train/date/class bookings. O(B) time and O(1) space. */
static void count_train_bookings(const SystemContext *context, OccupancyState *state)
{
    size_t i;
    for (i = 0; i < context->booking_bucket_count; ++i) {
        BookingRecord *record;
        for (record = context->booking_buckets[i]; record != NULL;
             record = record->hash_next) {
            const Booking *booking = &record->booking;
            if (booking->train_number == state->train_number &&
                booking->travel_class == state->travel_class &&
                strcmp(booking->journey_date, state->date) == 0) {
                if (booking->status == RAIL_STATUS_CNF) {
                    ++state->confirmed;
                } else if (booking->status == RAIL_STATUS_RAC) {
                    ++state->rac;
                } else {
                    ++state->waiting;
                }
            }
        }
    }
}

/* Print one train/date/class occupancy report. O(B+S+R+W). */
static void occupancy_report(SystemContext *context)
{
    unsigned train_number;
    char date[RAIL_DATE_LENGTH + 2];
    RailClass travel_class;
    const TrainNode *node;
    Trip *trip;
    OccupancyState state;
    size_t i;
    size_t free_seats = 0;
    if (!read_number("Train number: ", 1, UINT_MAX, &train_number) ||
        !read_text("Journey date (YYYY-MM-DD): ", date, sizeof(date)) ||
        !read_class(&travel_class)) {
        return;
    }
    if (!engine_valid_date(date)) {
        (void)fprintf(stderr, "Enter a valid calendar date as YYYY-MM-DD.\n");
        return;
    }
    node = avl_find_train(context->train_root, train_number);
    if (node == NULL) {
        (void)printf("Train not found.\n");
        return;
    }
    trip = engine_find_trip(context, train_number, date, travel_class);
    memset(&state, 0, sizeof(state));
    state.context = context;
    state.train_number = train_number;
    state.date = date;
    state.travel_class = travel_class;
    count_train_bookings(context, &state);
    if (trip != NULL) {
        for (i = 0; i < trip->seats.seat_count; ++i) {
            if (trip->seats.occupied_segments[i] == 0) {
                ++free_seats;
            }
        }
    } else {
        free_seats = (size_t)node->train.coaches[travel_class] *
            node->train.seats_per_coach[travel_class];
    }
    (void)printf("\n=== Train %u / %s / %s ===\n", train_number,
                 date, rail_class_name(travel_class));
    (void)printf("Confirmed: %u | Free whole seats: %u | RAC: %u | WL: %u\n",
                 (unsigned)state.confirmed, (unsigned)free_seats,
                 (unsigned)state.rac, (unsigned)state.waiting);
    engine_print_queues(trip);
}

/* Collect and apply admin changes. O(log T) for train insertion. */
static void add_or_update_train(SystemContext *context)
{
    Train train;
    unsigned count;
    unsigned value;
    size_t i;
    unsigned c;
    RailError error;
    memset(&train, 0, sizeof(train));
    if (!read_number("Train number: ", 1, UINT_MAX, &train.number) ||
        !read_text("Train name: ", train.name, sizeof(train.name)) ||
        !read_number("Route stations (2-16): ", 2, RAIL_MAX_STATIONS, &count)) {
        return;
    }
    train.station_count = count;
    for (i = 0; i < train.station_count; ++i) {
        char prompt[48];
        (void)snprintf(prompt, sizeof(prompt), "Station %u: ", (unsigned)(i + 1));
        if (!read_text(prompt, train.stations[i], sizeof(train.stations[i]))) {
            return;
        }
    }
    for (c = 0; c < RAIL_CLASS_COUNT; ++c) {
        (void)printf("Enable %s? (0=no, 1=yes)\n", rail_class_name((RailClass)c));
        if (!read_number("Choice: ", 0, 1, &value)) {
            return;
        }
        if (value == 1) {
            train.class_mask |= 1U << c;
            if (!read_number("Coaches (1-16): ", 1, RAIL_MAX_COACHES,
                             &train.coaches[c]) ||
                !read_number("Seats per coach (1-128): ", 1,
                             RAIL_MAX_SEATS_PER_COACH,
                             &train.seats_per_coach[c])) {
                return;
            }
            if (!read_number("Historical cancellation rate (0-100%): ",
                             0, 100, &value)) {
                return;
            }
            train.cancellation_rates[c] = (double)value / 100.0;
        }
    }
    if (!read_number("RAC capacity (0-512): ", 0, RAIL_MAX_RAC, &train.rac_limit) ||
        !read_number("Waiting-list capacity (0-4096): ", 0, RAIL_MAX_WL,
                     &train.wl_limit)) {
        return;
    }
    error = engine_add_train(context, &train);
    (void)printf("%s\n", error == RAIL_OK ? "Train saved." :
                 rail_error_message(error));
}

/* Admin menu for train maintenance and occupancy reports. O(action). */
static void admin_menu(SystemContext *context)
{
    unsigned choice;
    for (;;) {
        (void)printf("\n=== Admin ===\n1. Add/update train\n2. Remove train\n"
                     "3. Occupancy report\n0. Back\n");
        if (!read_number("Select: ", 0, 3, &choice)) {
            if (feof(stdin)) {
                return;
            }
            continue;
        }
        if (choice == 0) {
            return;
        }
        if (choice == 1) {
            add_or_update_train(context);
        } else if (choice == 2) {
            unsigned number;
            RailError error;
            if (read_number("Train number to remove: ", 1, UINT_MAX, &number)) {
                error = engine_remove_train(context, number);
                (void)printf("%s\n", error == RAIL_OK ? "Train removed." :
                             rail_error_message(error));
            }
        } else {
            occupancy_report(context);
        }
    }
}

/* Run the main menu until save-and-exit or EOF. O(actions * action cost). */
void ui_run(SystemContext *context)
{
    unsigned choice;
    context->show_queue_transitions = true;
    for (;;) {
        (void)printf("\n========== RailEase ==========\n"
                     "1. Search trains       2. Book ticket\n"
                     "3. Cancel ticket       4. PNR status\n"
                     "5. Confirmation chance 6. Train occupancy\n"
                     "7. Undo last action    8. Admin menu\n"
                     "9. Simulate N cancellations  0. Save and exit\n");
        if (!read_number("Select: ", 0, 9, &choice)) {
            if (feof(stdin)) {
                if (storage_save(context, "data") != RAIL_OK) {
                    (void)fprintf(stderr, "Could not save data on end-of-input.\n");
                }
                return;
            }
            continue;
        }
        switch (choice) {
        case 0:
            {
                RailError error = storage_save(context, "data");
                if (error == RAIL_OK) {
                    (void)printf("Data saved. Goodbye.\n");
                } else {
                    (void)fprintf(stderr, "Save failed: %s\n",
                                  rail_error_message(error));
                }
            }
            return;
        case 1:
            search_trains(context);
            break;
        case 2:
            book_ticket(context);
            break;
        case 3:
            cancel_ticket(context);
            break;
        case 4:
            show_pnr_status(context);
            break;
        case 5:
            show_probability(context);
            break;
        case 6:
            occupancy_report(context);
            break;
        case 7: {
            RailError error = engine_undo(context);
            (void)printf("%s\n", error == RAIL_OK ? "Undo complete." :
                         rail_error_message(error));
            break;
        }
        case 8:
            admin_menu(context);
            break;
        case 9: {
            unsigned count;
            if (read_number("Number of cancellations: ", 1, 100000, &count)) {
                size_t completed = engine_simulate_cancellations(context, count);
                (void)printf("Simulated %u cancellation(s).\n",
                             (unsigned)completed);
            }
            break;
        }
        default:
            break;
        }
    }
}
