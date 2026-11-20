#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"

/* Compare station/passenger names without case. O(n) time and O(1) space. */
static bool same_text(const char *a, const char *b)
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

/* Validate a Gregorian YYYY-MM-DD date. O(1) time and O(1) space. */
bool engine_valid_date(const char *date)
{
    size_t i;
    unsigned month;
    unsigned day;
    unsigned year;
    static const unsigned month_days[] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (date == NULL || strlen(date) != RAIL_DATE_LENGTH - 1 ||
        date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (i = 0; i < RAIL_DATE_LENGTH - 1; ++i) {
        if (i != 4 && i != 7 && !isdigit((unsigned char)date[i])) {
            return false;
        }
    }
    year = (unsigned)(date[0] - '0') * 1000U +
        (unsigned)(date[1] - '0') * 100U +
        (unsigned)(date[2] - '0') * 10U + (unsigned)(date[3] - '0');
    month = (unsigned)(date[5] - '0') * 10U + (unsigned)(date[6] - '0');
    day = (unsigned)(date[8] - '0') * 10U + (unsigned)(date[9] - '0');
    if (year == 0 || month == 0 || month > 12 || day == 0) {
        return false;
    }
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        return day <= 29;
    }
    return day <= month_days[month - 1];
}

static void format_status_message(const Booking *booking, char *message, size_t size);
static void record_history_event(SystemContext *context, BookingRecord *record);

/* Resolve a forward route interval. O(stations) time and O(1) space. */
RailError engine_route_range(const Train *train, const char *source,
                             const char *destination, unsigned *from,
                             unsigned *to)
{
    size_t i;
    bool source_found = false;
    if (train == NULL || source == NULL || destination == NULL ||
        from == NULL || to == NULL) {
        return RAIL_ERR_INVALID;
    }
    for (i = 0; i < train->station_count; ++i) {
        if (!source_found && same_text(train->stations[i], source)) {
            *from = (unsigned)i;
            source_found = true;
        } else if (source_found && same_text(train->stations[i], destination)) {
            *to = (unsigned)i;
            return RAIL_OK;
        }
    }
    return RAIL_ERR_INVALID;
}

/* Find one train/date/class state. O(Q) time and O(1) space. */
static Trip *find_trip_mutable(SystemContext *context, unsigned train_number,
                               const char *date, RailClass travel_class)
{
    Trip *trip;
    for (trip = context->trips; trip != NULL; trip = trip->next) {
        if (trip->train_number == train_number && trip->travel_class == travel_class &&
            strcmp(trip->date, date) == 0) {
            return trip;
        }
    }
    return NULL;
}

/* Find existing trip state. O(Q) time and O(1) space. */
Trip *engine_find_trip(SystemContext *context, unsigned train_number,
                       const char *date, RailClass travel_class)
{
    if (context == NULL || date == NULL) {
        return NULL;
    }
    return find_trip_mutable(context, train_number, date, travel_class);
}

/* Allocate an empty trip's structures. O(S) time and O(S) space. */
static Trip *create_trip(SystemContext *context, const Train *train,
                         const char *date, RailClass travel_class)
{
    Trip *trip = calloc(1, sizeof(*trip));
    size_t seat_count;
    unsigned rac_capacity;
    if (trip == NULL) {
        return NULL;
    }
    trip->train_number = train->number;
    trip->travel_class = travel_class;
    trip->segment_count = (unsigned)train->station_count - 1;
    trip->rac_limit = train->rac_limit;
    trip->wl_limit = train->wl_limit;
    (void)snprintf(trip->date, sizeof(trip->date), "%s", date);
    seat_count = (size_t)train->coaches[travel_class] *
        train->seats_per_coach[travel_class];
    rac_capacity = train->rac_limit == 0 ? 1 : train->rac_limit;
    if (seat_count == 0 ||
        seat_bitmap_init(&trip->seats, seat_count, trip->segment_count) != RAIL_OK ||
        queue_init(&trip->rac, rac_capacity) != RAIL_OK) {
        seat_bitmap_destroy(&trip->seats);
        queue_destroy(&trip->rac);
        free(trip);
        return NULL;
    }
    heap_init(&trip->waiting);
    trip->next = context->trips;
    context->trips = trip;
    ++context->trip_count;
    return trip;
}

/* Destroy one trip's owned structures. O(S+R+W) time and O(1) space. */
static void destroy_trip(Trip *trip)
{
    seat_bitmap_destroy(&trip->seats);
    queue_destroy(&trip->rac);
    heap_destroy(&trip->waiting);
    free(trip);
}

/* Remove all trip states for a train. O(Q+S) time and O(1) space. */
static void remove_train_trips(SystemContext *context, unsigned train_number)
{
    Trip **link = &context->trips;
    while (*link != NULL) {
        Trip *trip = *link;
        if (trip->train_number == train_number) {
            *link = trip->next;
            destroy_trip(trip);
            --context->trip_count;
        } else {
            link = &trip->next;
        }
    }
}

/* Check for active reservations on a train. O(B) time and O(1) space. */
static bool train_has_bookings(const SystemContext *context, unsigned train_number)
{
    size_t i;
    for (i = 0; i < context->booking_bucket_count; ++i) {
        const BookingRecord *record;
        for (record = context->booking_buckets[i]; record != NULL;
             record = record->hash_next) {
            if (record->booking.train_number == train_number) {
                return true;
            }
        }
    }
    return false;
}

/* Refresh both live position fields. O(R+W) time and O(1) space. */
static void update_queue_positions(Trip *trip)
{
    size_t i;
    size_t position = 1;
    BookingRecord *record;
    for (i = 0; i < trip->rac.size; ++i) {
        size_t index = (trip->rac.head + i) % trip->rac.capacity;
        record = trip->rac.items[index];
        record->booking.queue_position = i + 1;
    }
    for (record = trip->waiting_view; record != NULL; record = record->wl_next) {
        record->booking.queue_position = position++;
    }
}

/* Append a status event, reporting log failures. O(message length) time. */
static void emit_status(const SystemContext *context, const Booking *booking)
{
    char message[128];
    format_status_message(booking, message, sizeof(message));
    if (engine_notify(context, message) != RAIL_OK) {
        (void)fprintf(stderr, "Warning: could not append notification: %s\n", message);
    }
}

/* Format a status event. O(1) time and O(1) space. */
static void format_status_message(const Booking *booking, char *message, size_t size)
{
    (void)snprintf(message, size, "PNR %s status changed to %s",
                   booking->pnr, rail_status_name(booking->status));
}

/* Compare WL quota/timestamp order. O(1) time and O(1) space. */
static bool waiting_priority_before(const BookingRecord *a, const BookingRecord *b)
{
    return a->booking.quota < b->booking.quota ||
        (a->booking.quota == b->booking.quota &&
         a->booking.timestamp < b->booking.timestamp);
}

/* Promote the first RAC passenger with a usable seat. O(R*S) time. */
static bool promote_rac(SystemContext *context, Trip *trip, const Train *train)
{
    size_t i;
    BookingRecord *record;
    unsigned from;
    unsigned to;
    for (i = 0; i < trip->rac.size; ++i) {
        size_t index = (trip->rac.head + i) % trip->rac.capacity;
        BookingRecord *candidate = trip->rac.items[index];
        if (engine_route_range(train, candidate->booking.source,
                          candidate->booking.destination, &from, &to) == RAIL_OK) {
            int seat = seat_bitmap_find(&trip->seats, from, to);
            if (seat >= 0) {
                record = candidate;
                if (context->show_queue_transitions) {
                    (void)printf("Before RAC promotion:\n");
                    engine_print_queues(trip);
                }
                (void)queue_remove(&trip->rac, record);
                (void)seat_bitmap_occupy(&trip->seats, (size_t)seat, from, to);
                record->booking.status = RAIL_STATUS_CNF;
                record->booking.seat_number = seat + 1;
                record->booking.queue_position = 0;
                record_history_event(context, record);
                emit_status(context, &record->booking);
                if (context->show_queue_transitions) {
                    (void)printf("After RAC promotion:\n");
                    engine_print_queues(trip);
                }
                return true;
            }
        }
    }
    return false;
}

/* Promote the highest-priority eligible WL passenger. O(W*S) worst case. */
static bool promote_waiting(SystemContext *context, Trip *trip, const Train *train)
{
    BookingRecord *record = heap_peek(&trip->waiting);
    unsigned from = 0;
    unsigned to = 0;
    int seat = -1;
    if (record == NULL) {
        return false;
    }
    if (engine_route_range(train, record->booking.source, record->booking.destination,
                           &from, &to) == RAIL_OK) {
        seat = seat_bitmap_find(&trip->seats, from, to);
    }
    if (seat < 0 && trip->rac.size >= trip->rac_limit) {
        size_t i;
        BookingRecord *eligible = NULL;
        int eligible_seat = -1;
        unsigned eligible_from = 0;
        unsigned eligible_to = 0;
        for (i = 0; i < trip->waiting.size; ++i) {
            BookingRecord *candidate = trip->waiting.items[i];
            unsigned candidate_from;
            unsigned candidate_to;
            int candidate_seat;
            if (engine_route_range(train, candidate->booking.source,
                                   candidate->booking.destination,
                                   &candidate_from, &candidate_to) != RAIL_OK) {
                continue;
            }
            candidate_seat = seat_bitmap_find(&trip->seats, candidate_from,
                                              candidate_to);
            if (candidate_seat >= 0 &&
                (eligible == NULL || waiting_priority_before(candidate, eligible))) {
                eligible = candidate;
                eligible_seat = candidate_seat;
                eligible_from = candidate_from;
                eligible_to = candidate_to;
            }
        }
        if (eligible == NULL) {
            return false;
        }
        record = eligible;
        seat = eligible_seat;
        from = eligible_from;
        to = eligible_to;
    }
    if (context->show_queue_transitions) {
        (void)printf("Before WL promotion:\n");
        engine_print_queues(trip);
    }
    (void)heap_remove(&trip->waiting, record);
    wl_remove(&trip->waiting_view, record);
    if (seat >= 0) {
        (void)seat_bitmap_occupy(&trip->seats, (size_t)seat, from, to);
        record->booking.status = RAIL_STATUS_CNF;
        record->booking.seat_number = seat + 1;
        record->booking.queue_position = 0;
    } else if (trip->rac.size < trip->rac_limit) {
        if (queue_push(&trip->rac, record) != RAIL_OK) {
            (void)heap_push(&trip->waiting, record);
            wl_insert_ordered(&trip->waiting_view, record);
            return false;
        }
        record->booking.status = RAIL_STATUS_RAC;
        record->booking.seat_number = 0;
        record->booking.queue_position = trip->rac.size;
        record->booking.timestamp = context->next_timestamp;
    } else {
        (void)heap_push(&trip->waiting, record);
        wl_insert_ordered(&trip->waiting_view, record);
        return false;
    }
    record_history_event(context, record);
    emit_status(context, &record->booking);
    if (context->show_queue_transitions) {
        (void)printf("After WL promotion:\n");
        engine_print_queues(trip);
    }
    return true;
}

/* Reallocate one place and refresh positions. O((R+W+1)*S+W+log W). */
static void reallocate(SystemContext *context, Trip *trip, const Train *train)
{
    (void)promote_rac(context, trip, train);
    (void)promote_waiting(context, trip, train);
    update_queue_positions(trip);
}

/* Validate train limits and bounded strings. O(stations+classes) time. */
static bool valid_train(const Train *train)
{
    size_t i;
    unsigned class_index;
    if (train == NULL || train->number == 0 ||
        memchr(train->name, '\0', sizeof(train->name)) == NULL ||
        train->name[0] == '\0' ||
        train->station_count < 2 || train->station_count > RAIL_MAX_STATIONS ||
        train->rac_limit > RAIL_MAX_RAC || train->wl_limit > RAIL_MAX_WL ||
        (train->class_mask & ~((1U << RAIL_CLASS_COUNT) - 1U)) != 0) {
        return false;
    }
    for (i = 0; i < train->station_count; ++i) {
        if (memchr(train->stations[i], '\0', RAIL_MAX_NAME) == NULL ||
            train->stations[i][0] == '\0') {
            return false;
        }
    }
    for (class_index = 0; class_index < RAIL_CLASS_COUNT; ++class_index) {
        if (!(train->cancellation_rates[class_index] >= 0.0 &&
              train->cancellation_rates[class_index] <= 1.0)) {
            return false;
        }
        if ((train->class_mask & (1U << class_index)) != 0 &&
            (train->coaches[class_index] == 0 ||
             train->coaches[class_index] > RAIL_MAX_COACHES ||
             train->seats_per_coach[class_index] == 0 ||
             train->seats_per_coach[class_index] > RAIL_MAX_SEATS_PER_COACH)) {
            return false;
        }
    }
    return true;
}

/* Compare configured train fields. O(stations+classes) time and O(1) space. */
static bool trains_equal(const Train *a, const Train *b)
{
    unsigned c;
    size_t i;
    if (a->number != b->number || strcmp(a->name, b->name) != 0 ||
        a->station_count != b->station_count || a->class_mask != b->class_mask ||
        a->rac_limit != b->rac_limit || a->wl_limit != b->wl_limit) {
        return false;
    }
    for (i = 0; i < a->station_count; ++i) {
        if (strcmp(a->stations[i], b->stations[i]) != 0) {
            return false;
        }
    }
    for (c = 0; c < RAIL_CLASS_COUNT; ++c) {
        if (a->coaches[c] != b->coaches[c] ||
            a->seats_per_coach[c] != b->seats_per_coach[c] ||
            a->cancellation_rates[c] != b->cancellation_rates[c]) {
            return false;
        }
    }
    return true;
}

/* Build a date-major history key. O(1) time and O(1) space. */
static uint64_t date_history_key(const char *date, uint64_t timestamp)
{
    uint64_t day = 0;
    size_t i;
    for (i = 0; i < RAIL_DATE_LENGTH - 1; ++i) {
        if (date[i] != '-') {
            day = day * 10 + (uint64_t)(date[i] - '0');
        }
    }
    return day * UINT64_C(1000000000) + timestamp;
}

/* Insert a status event in the date AVL. O(log H) time and stack space. */
static void record_history_event(SystemContext *context, BookingRecord *record)
{
    uint64_t key = date_history_key(record->booking.journey_date,
                                    context->next_timestamp++);
    if (avl_insert_history(&context->history_root, key, &record->booking) != RAIL_OK) {
        (void)fprintf(stderr, "Warning: booking status history could not be indexed.\n");
    }
}

/* Restore RAC FIFO order from persistent booking timestamps. O(R) time. */
static RailError rac_insert_by_time(RacQueue *queue, BookingRecord *record)
{
    size_t position = 0;
    size_t i;
    if (queue->size >= queue->capacity) {
        return RAIL_ERR_FULL;
    }
    while (position < queue->size) {
        size_t index = (queue->head + position) % queue->capacity;
        if (queue->items[index]->booking.timestamp > record->booking.timestamp) {
            break;
        }
        ++position;
    }
    for (i = queue->size; i > position; --i) {
        size_t destination = (queue->head + i) % queue->capacity;
        size_t source = (queue->head + i - 1) % queue->capacity;
        queue->items[destination] = queue->items[source];
    }
    queue->items[(queue->head + position) % queue->capacity] = record;
    ++queue->size;
    return RAIL_OK;
}

/* Reject same passenger/train/date active duplicates. O(B) time, O(1) space. */
static bool duplicate_passenger(const SystemContext *context, const Passenger *passenger,
                                unsigned train_number, const char *date)
{
    size_t i;
    for (i = 0; i < context->booking_bucket_count; ++i) {
        const BookingRecord *record;
        for (record = context->booking_buckets[i]; record != NULL;
             record = record->hash_next) {
            if (record->booking.train_number == train_number &&
                strcmp(record->booking.journey_date, date) == 0 &&
                same_text(record->booking.passenger.name, passenger->name) &&
                strcmp(record->booking.passenger.phone, passenger->phone) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* Initialize system defaults. O(1) time and O(1) space. */
void engine_init(SystemContext *context)
{
    if (context == NULL) {
        return;
    }
    memset(context, 0, sizeof(*context));
    context->next_timestamp = 1;
    context->next_pnr = UINT64_C(1000000000);
    context->cancellation_rates[RAIL_CLASS_SL] = 0.12;
    context->cancellation_rates[RAIL_CLASS_3A] = 0.10;
    context->cancellation_rates[RAIL_CLASS_2A] = 0.08;
    context->cancellation_rates[RAIL_CLASS_1A] = 0.05;
    (void)snprintf(context->notification_path, sizeof(context->notification_path),
                   "data/notifications.log");
}

/* Release all owned structures. O(T+B) time and O(log T) recursion space. */
void engine_destroy(SystemContext *context)
{
    Trip *trip;
    if (context == NULL) {
        return;
    }
    trip = context->trips;
    while (trip != NULL) {
        Trip *next = trip->next;
        destroy_trip(trip);
        trip = next;
    }
    context->trips = NULL;
    context->trip_count = 0;
    context->train_count = 0;
    booking_hash_destroy(context);
    avl_destroy(context->train_root);
    avl_destroy(context->history_root);
    context->train_root = NULL;
    context->history_root = NULL;
    undo_destroy(context);
}

/* Add/update a train. O(B+Q*S+log T) time and O(log T) stack space. */
RailError engine_add_train(SystemContext *context, const Train *train)
{
    const TrainNode *existing;
    RailError error;
    if (context == NULL || !valid_train(train)) {
        return RAIL_ERR_INVALID;
    }
    if (train->class_mask == 0) {
        return RAIL_ERR_INVALID;
    }
    existing = avl_find_train(context->train_root, train->number);
    if (existing == NULL && context->train_count >= RAIL_MAX_TRAINS) {
        return RAIL_ERR_FULL;
    }
    if (existing != NULL && !trains_equal(&existing->train, train)) {
        if (train_has_bookings(context, train->number)) {
            return RAIL_ERR_BUSY;
        }
        remove_train_trips(context, train->number);
    }
    error = avl_insert_train(&context->train_root, train);
    if (error == RAIL_OK && existing == NULL) {
        ++context->train_count;
    }
    return error;
}

/* Remove a train with no active bookings. O(B+Q*S+log T) time. */
RailError engine_remove_train(SystemContext *context, unsigned train_number)
{
    if (context == NULL || train_number == 0) {
        return RAIL_ERR_INVALID;
    }
    if (train_has_bookings(context, train_number)) {
        return RAIL_ERR_BUSY;
    }
    {
        RailError error = avl_remove_train(&context->train_root, train_number);
        if (error == RAIL_OK) {
            --context->train_count;
            remove_train_trips(context, train_number);
        }
        return error;
    }
}

/* Return live status. Average O(1)+O(Q+W) time and O(1) space. */
RailError engine_status(const SystemContext *context, const char *pnr,
                        Booking *booking, size_t *position)
{
    BookingRecord *record;
    Trip *trip;
    if (context == NULL || pnr == NULL || booking == NULL || position == NULL) {
        return RAIL_ERR_INVALID;
    }
    record = booking_hash_find(context, pnr);
    if (record == NULL) {
        return RAIL_ERR_NOT_FOUND;
    }
    *booking = record->booking;
    *position = 0;
    if (booking->status != RAIL_STATUS_CNF) {
        trip = find_trip_mutable((SystemContext *)context, booking->train_number,
                                 booking->journey_date, booking->travel_class);
        if (trip != NULL) {
            *position = booking->status == RAIL_STATUS_RAC ?
                booking->queue_position :
                wl_position(trip->waiting_view, record);
            booking->queue_position = *position;
        }
    }
    return RAIL_OK;
}

/* Restore a booking. O(Q+S+R+W+log W+log H) time. */
RailError engine_restore_booking(SystemContext *context, const Booking *booking)
{
    const TrainNode *node;
    const Train *train;
    BookingRecord *record;
    Trip *trip;
    unsigned from;
    unsigned to;
    bool seat_reserved = false;
    RailError error;
    if (context == NULL || booking == NULL || booking->pnr[0] == '\0' ||
        memchr(booking->journey_date, '\0',
               sizeof(booking->journey_date)) == NULL ||
        !engine_valid_date(booking->journey_date) ||
        memchr(booking->pnr, '\0', sizeof(booking->pnr)) == NULL ||
        strlen(booking->pnr) != RAIL_PNR_LENGTH ||
        memchr(booking->passenger.name, '\0',
               sizeof(booking->passenger.name)) == NULL ||
        memchr(booking->passenger.phone, '\0',
               sizeof(booking->passenger.phone)) == NULL ||
        memchr(booking->passenger.berth_preference, '\0',
               sizeof(booking->passenger.berth_preference)) == NULL ||
        memchr(booking->source, '\0', sizeof(booking->source)) == NULL ||
        memchr(booking->destination, '\0', sizeof(booking->destination)) == NULL ||
        booking->passenger.name[0] == '\0' || booking->passenger.phone[0] == '\0' ||
        booking->passenger.age == 0 || booking->passenger.age > 120 ||
        (booking->passenger.gender != 'M' && booking->passenger.gender != 'F' &&
         booking->passenger.gender != 'O') ||
        booking->initial_status < RAIL_STATUS_CNF ||
        booking->initial_status > RAIL_STATUS_WL ||
        booking->status < RAIL_STATUS_CNF ||
        booking->status > RAIL_STATUS_WL ||
        booking->travel_class < RAIL_CLASS_SL ||
        booking->travel_class >= RAIL_CLASS_COUNT ||
        booking->quota < RAIL_QUOTA_GENERAL || booking->quota > RAIL_QUOTA_SENIOR) {
        return RAIL_ERR_INVALID;
    }
    {
        size_t i;
        for (i = 0; i < RAIL_PNR_LENGTH; ++i) {
            if (booking->pnr[i] < '0' || booking->pnr[i] > '9') {
                return RAIL_ERR_INVALID;
            }
        }
    }
    if (context->booking_count >= RAIL_MAX_BOOKINGS) {
        return RAIL_ERR_FULL;
    }
    node = avl_find_train(context->train_root, booking->train_number);
    if (node == NULL || booking->travel_class >= RAIL_CLASS_COUNT ||
        (node->train.class_mask & (1U << booking->travel_class)) == 0) {
        return RAIL_ERR_NOT_FOUND;
    }
    train = &node->train;
    error = engine_route_range(train, booking->source, booking->destination, &from, &to);
    if (error != RAIL_OK) {
        return error;
    }
    trip = find_trip_mutable(context, booking->train_number, booking->journey_date,
                             booking->travel_class);
    if (trip == NULL) {
        trip = create_trip(context, train, booking->journey_date, booking->travel_class);
        if (trip == NULL) {
            return RAIL_ERR_MEMORY;
        }
    }
    record = calloc(1, sizeof(*record));
    if (record == NULL) {
        return RAIL_ERR_MEMORY;
    }
    record->booking = *booking;
    record->heap_index = SIZE_MAX;
    switch (booking->status) {
    case RAIL_STATUS_CNF:
        if (booking->seat_number < 1 ||
            (size_t)booking->seat_number > trip->seats.seat_count) {
            free(record);
            return RAIL_ERR_CORRUPT;
        }
        error = seat_bitmap_occupy(&trip->seats, (size_t)booking->seat_number - 1,
                                   from, to);
        seat_reserved = error == RAIL_OK;
        break;
    case RAIL_STATUS_RAC:
        error = trip->rac.size >= trip->rac_limit ? RAIL_ERR_FULL :
            rac_insert_by_time(&trip->rac, record);
        break;
    case RAIL_STATUS_WL:
        error = trip->waiting.size >= trip->wl_limit ? RAIL_ERR_FULL :
            heap_push(&trip->waiting, record);
        if (error == RAIL_OK) {
            wl_insert_ordered(&trip->waiting_view, record);
        }
        break;
    default:
        error = RAIL_ERR_CORRUPT;
        break;
    }
    if (error == RAIL_OK) {
        error = booking_hash_insert(context, record);
    }
    if (error != RAIL_OK) {
        if (record->heap_index != SIZE_MAX) {
            (void)heap_remove(&trip->waiting, record);
            wl_remove(&trip->waiting_view, record);
        } else if (booking->status == RAIL_STATUS_RAC) {
            (void)queue_remove(&trip->rac, record);
        } else if (booking->status == RAIL_STATUS_CNF && seat_reserved) {
            (void)seat_bitmap_release(&trip->seats, (size_t)booking->seat_number - 1,
                                      from, to);
        }
        free(record);
        return error;
    }
    if (booking->timestamp >= context->next_timestamp) {
        context->next_timestamp = booking->timestamp + 1;
    }
    if (avl_insert_history(&context->history_root,
                           date_history_key(booking->journey_date,
                                            booking->timestamp),
                           booking) != RAIL_OK) {
        (void)fprintf(stderr, "Warning: restored booking history could not be indexed.\n");
    }
    update_queue_positions(trip);
    return RAIL_OK;
}

/* Book a ticket. O(B+Q+S+W+log W) time and O(1) auxiliary space. */
RailError engine_book(SystemContext *context, const Passenger *passenger,
                      unsigned train_number, const char *date, const char *source,
                      const char *destination, RailClass travel_class,
                      QuotaType quota, char out_pnr[RAIL_PNR_LENGTH + 1])
{
    const TrainNode *node;
    const Train *train;
    Trip *trip;
    Booking booking;
    BookingRecord *record;
    unsigned from;
    unsigned to;
    int seat;
    RailError error;
    char pnr[RAIL_PNR_LENGTH + 1];
    if (context == NULL || passenger == NULL || out_pnr == NULL ||
        memchr(passenger->name, '\0', sizeof(passenger->name)) == NULL ||
        memchr(passenger->phone, '\0', sizeof(passenger->phone)) == NULL ||
        memchr(passenger->berth_preference, '\0',
               sizeof(passenger->berth_preference)) == NULL ||
        passenger->name[0] == '\0' || passenger->phone[0] == '\0' ||
        passenger->age == 0 || passenger->age > 120 ||
        (passenger->gender != 'M' && passenger->gender != 'F' &&
         passenger->gender != 'O') ||
        !engine_valid_date(date) || source == NULL || destination == NULL ||
        source[0] == '\0' || destination[0] == '\0' ||
        strlen(source) >= RAIL_MAX_NAME || strlen(destination) >= RAIL_MAX_NAME ||
        travel_class < RAIL_CLASS_SL || travel_class >= RAIL_CLASS_COUNT ||
        quota < RAIL_QUOTA_GENERAL || quota > RAIL_QUOTA_SENIOR) {
        return RAIL_ERR_INVALID;
    }
    node = avl_find_train(context->train_root, train_number);
    if (node == NULL) {
        return RAIL_ERR_NOT_FOUND;
    }
    train = &node->train;
    if ((train->class_mask & (1U << travel_class)) == 0) {
        return RAIL_ERR_INVALID;
    }
    if (duplicate_passenger(context, passenger, train_number, date)) {
        return RAIL_ERR_DUPLICATE;
    }
    if (context->next_pnr > UINT64_C(9999999999) ||
        context->booking_count >= RAIL_MAX_BOOKINGS) {
        return RAIL_ERR_FULL;
    }
    memset(&booking, 0, sizeof(booking));
    (void)snprintf(pnr, sizeof(pnr), "%010llu",
                   (unsigned long long)context->next_pnr);
    (void)snprintf(booking.pnr, sizeof(booking.pnr), "%s", pnr);
    booking.passenger = *passenger;
    booking.train_number = train_number;
    (void)snprintf(booking.journey_date, sizeof(booking.journey_date), "%s", date);
    (void)snprintf(booking.source, sizeof(booking.source), "%s", source);
    (void)snprintf(booking.destination, sizeof(booking.destination), "%s", destination);
    booking.travel_class = travel_class;
    booking.timestamp = context->next_timestamp;
    booking.quota = quota;
    error = engine_route_range(train, source, destination, &from, &to);
    if (error != RAIL_OK) {
        return error;
    }
    trip = find_trip_mutable(context, train_number, date, travel_class);
    if (trip == NULL) {
        trip = create_trip(context, train, date, travel_class);
        if (trip == NULL) {
            return RAIL_ERR_MEMORY;
        }
    }
    seat = seat_bitmap_find(&trip->seats, from, to);
    if (seat >= 0) {
        booking.status = RAIL_STATUS_CNF;
        booking.seat_number = seat + 1;
    } else if (trip->rac.size < trip->rac_limit) {
        booking.status = RAIL_STATUS_RAC;
        booking.queue_position = trip->rac.size + 1;
    } else if (trip->waiting.size < trip->wl_limit) {
        booking.status = RAIL_STATUS_WL;
        booking.queue_position = trip->waiting.size + 1;
    } else {
        return RAIL_ERR_FULL;
    }
    booking.initial_status = booking.status;
    record = calloc(1, sizeof(*record));
    if (record == NULL) {
        return RAIL_ERR_MEMORY;
    }
    record->booking = booking;
    record->heap_index = SIZE_MAX;
    if (booking.status == RAIL_STATUS_CNF) {
        error = seat_bitmap_occupy(&trip->seats, (size_t)seat, from, to);
    } else if (booking.status == RAIL_STATUS_RAC) {
        error = queue_push(&trip->rac, record);
    } else {
        error = heap_push(&trip->waiting, record);
        if (error == RAIL_OK) {
            wl_insert_ordered(&trip->waiting_view, record);
        }
    }
    if (error == RAIL_OK) {
        error = booking_hash_insert(context, record);
    }
    if (error != RAIL_OK) {
        if (record->heap_index != SIZE_MAX) {
            (void)heap_remove(&trip->waiting, record);
            wl_remove(&trip->waiting_view, record);
        } else if (booking.status == RAIL_STATUS_RAC) {
            (void)queue_remove(&trip->rac, record);
        } else if (booking.status == RAIL_STATUS_CNF) {
            (void)seat_bitmap_release(&trip->seats, (size_t)seat, from, to);
        }
        free(record);
        return error;
    }
    ++context->next_timestamp;
    ++context->next_pnr;
    update_queue_positions(trip);
    if (record->booking.status != RAIL_STATUS_CNF) {
        record->booking.original_position = (unsigned)record->booking.queue_position;
    }
    if (avl_insert_history(&context->history_root,
                           date_history_key(date, booking.timestamp),
                           &record->booking) != RAIL_OK) {
        (void)fprintf(stderr, "Warning: booking history could not be indexed.\n");
    }
    (void)snprintf(out_pnr, RAIL_PNR_LENGTH + 1, "%s", pnr);
    emit_status(context, &record->booking);
    if (undo_push(context, UNDO_BOOKING, &record->booking) != RAIL_OK) {
        (void)fprintf(stderr, "Warning: booking succeeded but undo snapshot could not be stored.\n");
    }
    return RAIL_OK;
}

/* Calculate the illustrative date-tier refund. O(1) time and space. */
static double refund_for_booking(const Booking *booking)
{
    static const double fares[RAIL_CLASS_COUNT] = {300.0, 800.0, 1200.0, 1800.0};
    int year;
    int month;
    int day;
    time_t now = time(NULL);
    struct tm departure;
    struct tm *current = localtime(&now);
    time_t departure_time;
    double days;
    double fraction;
    if (sscanf(booking->journey_date, "%4d-%2d-%2d", &year, &month, &day) != 3 ||
        current == NULL) {
        return 0.0;
    }
    departure = *current;
    departure.tm_year = year - 1900;
    departure.tm_mon = month - 1;
    departure.tm_mday = day;
    departure.tm_hour = 0;
    departure.tm_min = 0;
    departure.tm_sec = 0;
    departure.tm_isdst = -1;
    departure_time = mktime(&departure);
    if (departure_time == (time_t)-1) {
        return 0.0;
    }
    days = difftime(departure_time, now) / 86400.0;
    fraction = days > 2.0 ? 1.0 : (days > 1.0 ? 0.75 : 0.50);
    return fares[booking->travel_class] * fraction;
}

/* Remove a record and reallocate. O(B+Q+(R+W+1)*S+W+log H). */
static RailError cancel_core(SystemContext *context, const char *pnr,
                             double *refund, bool save_undo)
{
    BookingRecord *record;
    Booking snapshot;
    Trip *trip;
    const TrainNode *node;
    unsigned from = 0;
    unsigned to = 0;
    uint64_t history_key;
    RailError error;
    if (context == NULL || pnr == NULL) {
        return RAIL_ERR_INVALID;
    }
    record = booking_hash_find(context, pnr);
    if (record == NULL) {
        return RAIL_ERR_NOT_FOUND;
    }
    snapshot = record->booking;
    node = avl_find_train(context->train_root, snapshot.train_number);
    trip = find_trip_mutable(context, snapshot.train_number, snapshot.journey_date,
                             snapshot.travel_class);
    if (node == NULL || trip == NULL) {
        return RAIL_ERR_CORRUPT;
    }
    if (snapshot.status == RAIL_STATUS_CNF) {
        error = engine_route_range(&node->train, snapshot.source, snapshot.destination,
                              &from, &to);
        if (error != RAIL_OK) {
            return error;
        }
    }
    if (save_undo && undo_push(context, UNDO_CANCELLATION, &snapshot) != RAIL_OK) {
        return RAIL_ERR_MEMORY;
    }
    history_key = date_history_key(snapshot.journey_date, context->next_timestamp);
    error = avl_insert_history(&context->history_root, history_key, &snapshot);
    if (error != RAIL_OK) {
        if (save_undo) {
            UndoKind kind;
            Booking discarded;
            (void)undo_pop(context, &kind, &discarded);
        }
        return error;
    }
    ++context->next_timestamp;
    if (snapshot.status == RAIL_STATUS_CNF) {
        error = seat_bitmap_release(&trip->seats,
                                    (size_t)snapshot.seat_number - 1, from, to);
    } else if (snapshot.status == RAIL_STATUS_RAC) {
        error = queue_remove(&trip->rac, record) ? RAIL_OK : RAIL_ERR_CORRUPT;
    } else {
        error = heap_remove(&trip->waiting, record) != NULL ?
            RAIL_OK : RAIL_ERR_CORRUPT;
        if (error == RAIL_OK) {
            wl_remove(&trip->waiting_view, record);
        }
    }
    if (error != RAIL_OK) {
        if (save_undo) {
            UndoKind kind;
            Booking discarded;
            (void)undo_pop(context, &kind, &discarded);
        }
        return error;
    }
    record = booking_hash_remove(context, pnr);
    if (record == NULL) {
        return RAIL_ERR_CORRUPT;
    }
    if (refund != NULL) {
        *refund = refund_for_booking(&snapshot);
    }
    {
        char message[128];
        (void)snprintf(message, sizeof(message), "PNR %s cancelled", snapshot.pnr);
        if (engine_notify(context, message) != RAIL_OK) {
            (void)fprintf(stderr, "Warning: could not append notification: %s\n", message);
        }
    }
    free(record);
    reallocate(context, trip, &node->train);
    return RAIL_OK;
}

/* Cancel a PNR. O(B+Q+(R+W+1)*S+W+log H) time. */
RailError engine_cancel(SystemContext *context, const char *pnr, double *refund)
{
    return cancel_core(context, pnr, refund, true);
}

/* Restore cancellation in the best available state. O(Q+S+R+W+log W). */
static RailError restore_cancelled(SystemContext *context, Booking *booking)
{
    RailError error = engine_restore_booking(context, booking);
    if (error != RAIL_ERR_FULL) {
        return error;
    }
    if (booking->status == RAIL_STATUS_CNF) {
        booking->status = RAIL_STATUS_RAC;
        booking->seat_number = 0;
        booking->queue_position = 0;
        error = engine_restore_booking(context, booking);
        if (error != RAIL_ERR_FULL) {
            return error;
        }
    }
    if (booking->status != RAIL_STATUS_WL) {
        booking->status = RAIL_STATUS_WL;
        booking->seat_number = 0;
        booking->queue_position = 0;
    }
    return engine_restore_booking(context, booking);
}

/* Undo the newest action. O(B+Q+(R+W+1)*S+W+log H). */
RailError engine_undo(SystemContext *context)
{
    UndoKind kind;
    Booking booking;
    RailError error;
    if (context == NULL || !undo_pop(context, &kind, &booking)) {
        return RAIL_ERR_NOT_FOUND;
    }
    if (kind == UNDO_BOOKING) {
        return cancel_core(context, booking.pnr, NULL, false);
    }
    error = restore_cancelled(context, &booking);
    if (error != RAIL_OK) {
        if (undo_push(context, kind, &booking) != RAIL_OK) {
            (void)fprintf(stderr, "Error: could not restore failed undo snapshot.\n");
        }
        return error;
    }
    emit_status(context, &booking);
    return RAIL_OK;
}

/* Append a notification line. O(message length) time and O(1) space. */
RailError engine_notify(const SystemContext *context, const char *message)
{
    FILE *file;
    bool ok;
    if (context == NULL || message == NULL || context->notification_path[0] == '\0') {
        return RAIL_ERR_INVALID;
    }
    file = fopen(context->notification_path, "a");
    if (file == NULL) {
        return RAIL_ERR_IO;
    }
    ok = fprintf(file, "%s\n", message) >= 0;
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        return RAIL_ERR_IO;
    }
    return RAIL_OK;
}

/* Print RAC and waiting-list state. O(R+W) time and O(1) space. */
void engine_print_queues(const Trip *trip)
{
    size_t i;
    const BookingRecord *record;
    if (trip == NULL) {
        (void)printf("No bookings for this train/date/class.\n");
        return;
    }
    (void)printf("RAC (%u):", (unsigned)trip->rac.size);
    for (i = 0; i < trip->rac.size; ++i) {
        size_t index = (trip->rac.head + i) % trip->rac.capacity;
        (void)printf(" %s", trip->rac.items[index]->booking.pnr);
    }
    (void)printf("\nWL (%u):", (unsigned)trip->waiting.size);
    for (record = trip->waiting_view; record != NULL; record = record->wl_next) {
        (void)printf(" %s", record->booking.pnr);
    }
    (void)printf("\n");
}

/* Cancel N pseudo-random bookings. O(N*(B+Q+(R+W+1)*S+W)). */
size_t engine_simulate_cancellations(SystemContext *context, size_t count)
{
    size_t cancelled = 0;
    if (context == NULL) {
        return 0;
    }
    while (cancelled < count) {
        size_t bucket;
        size_t confirmed = 0;
        size_t target;
        char pnr[RAIL_PNR_LENGTH + 1] = "";
        for (bucket = 0; bucket < context->booking_bucket_count; ++bucket) {
            BookingRecord *record;
            for (record = context->booking_buckets[bucket]; record != NULL;
                 record = record->hash_next) {
                if (record->booking.status == RAIL_STATUS_CNF) {
                    ++confirmed;
                }
            }
        }
        if (confirmed == 0) {
            break;
        }
        target = (size_t)((context->next_timestamp * UINT64_C(1103515245) +
                           UINT64_C(12345)) % confirmed);
        for (bucket = 0; bucket < context->booking_bucket_count &&
                         pnr[0] == '\0'; ++bucket) {
            BookingRecord *record;
            for (record = context->booking_buckets[bucket]; record != NULL;
                 record = record->hash_next) {
                if (record->booking.status == RAIL_STATUS_CNF) {
                    if (target == 0) {
                        (void)snprintf(pnr, sizeof(pnr), "%s",
                                       record->booking.pnr);
                        break;
                    }
                    --target;
                }
            }
        }
        {
            Booking before;
            size_t position;
            Trip *trip;
            if (engine_status(context, pnr, &before, &position) != RAIL_OK ||
                (trip = engine_find_trip(context, before.train_number,
                                         before.journey_date,
                                         before.travel_class)) == NULL) {
                break;
            }
            if (context->show_queue_transitions) {
                (void)printf("Before simulated cancellation:\n");
                engine_print_queues(trip);
            }
            if (engine_cancel(context, pnr, NULL) != RAIL_OK) {
                break;
            }
            if (context->show_queue_transitions) {
                (void)printf("After simulated cancellation:\n");
                engine_print_queues(trip);
            }
        }
        ++cancelled;
    }
    return cancelled;
}

