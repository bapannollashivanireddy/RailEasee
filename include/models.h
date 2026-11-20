#ifndef RAILEASE_MODELS_H
#define RAILEASE_MODELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RAIL_MAX_NAME 64
#define RAIL_MAX_STATIONS 16
#define RAIL_MAX_CLASSES 4
#define RAIL_MAX_COACHES 16
#define RAIL_MAX_SEATS_PER_COACH 128
#define RAIL_MAX_PHONE 24
#define RAIL_PNR_LENGTH 10
#define RAIL_HASH_BUCKETS 1021
#define RAIL_MAX_RAC 512
#define RAIL_MAX_WL 4096
#define RAIL_MAX_BOOKINGS 100000
#define RAIL_MAX_TRAINS 10000
#define RAIL_MAX_JOURNEY_SEGMENTS (RAIL_MAX_STATIONS - 1)
#define RAIL_CLASS_COUNT 4
#define RAIL_QUOTA_COUNT 4
#define RAIL_DATE_LENGTH 11

typedef enum {
    RAIL_OK = 0,
    RAIL_ERR_INVALID,
    RAIL_ERR_NOT_FOUND,
    RAIL_ERR_FULL,
    RAIL_ERR_BUSY,
    RAIL_ERR_MEMORY,
    RAIL_ERR_DUPLICATE,
    RAIL_ERR_IO,
    RAIL_ERR_CORRUPT
} RailError;

typedef enum {
    RAIL_CLASS_SL = 0,
    RAIL_CLASS_3A,
    RAIL_CLASS_2A,
    RAIL_CLASS_1A
} RailClass;

typedef enum {
    RAIL_STATUS_CNF = 0,
    RAIL_STATUS_RAC,
    RAIL_STATUS_WL
} BookingStatus;

typedef enum {
    RAIL_QUOTA_GENERAL = 0,
    RAIL_QUOTA_TATKAL,
    RAIL_QUOTA_LADIES,
    RAIL_QUOTA_SENIOR
} QuotaType;

typedef struct {
    char name[RAIL_MAX_NAME];
    unsigned age;
    char gender;
    char berth_preference[12];
    char phone[RAIL_MAX_PHONE];
} Passenger;

typedef struct {
    unsigned number;
    char name[RAIL_MAX_NAME];
    size_t station_count;
    char stations[RAIL_MAX_STATIONS][RAIL_MAX_NAME];
    unsigned class_mask;
    unsigned coaches[RAIL_MAX_CLASSES];
    unsigned seats_per_coach[RAIL_MAX_CLASSES];
    double cancellation_rates[RAIL_MAX_CLASSES];
    unsigned rac_limit;
    unsigned wl_limit;
} Train;

typedef struct {
    char pnr[RAIL_PNR_LENGTH + 1];
    Passenger passenger;
    unsigned train_number;
    char journey_date[RAIL_DATE_LENGTH];
    char source[RAIL_MAX_NAME];
    char destination[RAIL_MAX_NAME];
    RailClass travel_class;
    BookingStatus status;
    BookingStatus initial_status;
    int seat_number;
    size_t queue_position;
    uint64_t timestamp;
    QuotaType quota;
    unsigned original_position;
} Booking;

typedef struct {
    uint64_t key;
    Booking booking;
} HistoryEntry;

struct TrainNode;
struct BookingRecord;
struct Trip;
struct UndoNode;

typedef struct {
    struct TrainNode *train_root;
    struct TrainNode *history_root;
    struct BookingRecord **booking_buckets;
    size_t booking_bucket_count;
    struct Trip *trips;
    size_t trip_count;
    size_t trip_capacity;
    struct UndoNode *undo_top;
    size_t train_count;
    size_t booking_count;
    bool show_queue_transitions;
    uint64_t next_timestamp;
    uint64_t next_pnr;
    double cancellation_rates[RAIL_CLASS_COUNT];
    char notification_path[260];
} SystemContext;

/* Purpose: name a class. Params: enum value. Returns: label or INVALID. O(1). */
const char *rail_class_name(RailClass value);

/* Purpose: name a status. Params: enum value. Returns: label or INVALID. O(1). */
const char *rail_status_name(BookingStatus value);

/* Purpose: name a quota. Params: enum value. Returns: label or INVALID. O(1). */
const char *rail_quota_name(QuotaType value);

/* Purpose: describe an error. Params: error code. Returns: message. O(1). */
const char *rail_error_message(RailError error);

#endif
