#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"

#define STORAGE_VERSION 3U
#define STORAGE_PATH_SIZE 512

#ifdef _WIN32
#define STORAGE_SEPARATOR "\\"
#else
#define STORAGE_SEPARATOR "/"
#endif

typedef struct {
    char magic[8];
    uint32_t version;
    uint64_t count;
    uint64_t next_pnr;
    uint64_t next_timestamp;
} StorageHeader;

typedef struct {
    FILE *file;
    bool ok;
    uint64_t count;
} TrainWriter;

/* Join a directory and filename portably. O(path length) time and space. */
static bool make_path(char *output, size_t size, const char *directory,
                      const char *filename)
{
    int written;
    size_t length;
    if (directory == NULL || filename == NULL) {
        return false;
    }
    length = strlen(directory);
    written = snprintf(output, size, "%s%s%s", directory,
                       length > 0 && (directory[length - 1] == '/' ||
                                      directory[length - 1] == '\\') ? "" :
                       STORAGE_SEPARATOR, filename);
    return written >= 0 && (size_t)written < size;
}

/* Stream one train record and retain write status. O(1) time and space. */
static void write_train(const Train *train, void *user_data)
{
    TrainWriter *writer = user_data;
    if (writer->ok && fwrite(train, sizeof(*train), 1, writer->file) != 1) {
        writer->ok = false;
    }
    ++writer->count;
}

/* Close a file and combine close status. O(1) time and space. */
static bool close_checked(FILE *file, bool ok)
{
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

/* Save trains and active bookings. O(T+B) time and O(log T) stack space. */
RailError storage_save(SystemContext *context, const char *directory)
{
    char path[STORAGE_PATH_SIZE];
    FILE *file;
    StorageHeader header;
    TrainWriter writer;
    size_t i;
    uint64_t written = 0;
    if (context == NULL || directory == NULL) {
        return RAIL_ERR_INVALID;
    }
    if (!make_path(path, sizeof(path), directory, "trains.dat")) {
        return RAIL_ERR_INVALID;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return RAIL_ERR_IO;
    }
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "RTRAIN1", 7);
    header.version = STORAGE_VERSION;
    header.count = (uint64_t)context->train_count;
    header.next_pnr = context->next_pnr;
    header.next_timestamp = context->next_timestamp;
    if (fwrite(&header, sizeof(header), 1, file) != 1) {
        (void)close_checked(file, false);
        return RAIL_ERR_IO;
    }
    writer.file = file;
    writer.ok = true;
    writer.count = 0;
    avl_visit_trains(context->train_root, write_train, &writer);
    {
        bool ok = writer.ok && writer.count == header.count;
        if (!close_checked(file, ok)) {
            return RAIL_ERR_IO;
        }
    }
    if (!writer.ok || writer.count != header.count) {
        return RAIL_ERR_IO;
    }
    if (!make_path(path, sizeof(path), directory, "bookings.dat")) {
        return RAIL_ERR_INVALID;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return RAIL_ERR_IO;
    }
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "RBOOK01", 7);
    header.version = STORAGE_VERSION;
    header.count = (uint64_t)context->booking_count;
    header.next_pnr = context->next_pnr;
    header.next_timestamp = context->next_timestamp;
    if (fwrite(&header, sizeof(header), 1, file) != 1) {
        (void)close_checked(file, false);
        return RAIL_ERR_IO;
    }
    for (i = 0; i < context->booking_bucket_count; ++i) {
        BookingRecord *record;
        for (record = context->booking_buckets[i]; record != NULL;
             record = record->hash_next) {
            if (fwrite(&record->booking, sizeof(record->booking), 1, file) != 1) {
                (void)close_checked(file, false);
                return RAIL_ERR_IO;
            }
            ++written;
        }
    }
    return close_checked(file, written == header.count) ? RAIL_OK : RAIL_ERR_IO;
}

/* Validate a versioned storage header. O(1) time and space. */
static bool valid_header(const StorageHeader *header, const char *magic,
                         uint64_t maximum)
{
    return memcmp(header->magic, magic, 8) == 0 &&
        header->version == STORAGE_VERSION && header->count <= maximum &&
        header->next_pnr >= UINT64_C(1000000000) &&
        header->next_pnr <= UINT64_C(10000000000) &&
        header->next_timestamp > 0;
}

/* Validate a decoded train before indexing it. O(stations+classes). */
static bool valid_train_record(const Train *train)
{
    size_t i;
    unsigned c;
    if (train->number == 0 || memchr(train->name, '\0', sizeof(train->name)) == NULL ||
        train->name[0] == '\0' || train->station_count < 2 ||
        train->station_count > RAIL_MAX_STATIONS ||
        train->rac_limit > RAIL_MAX_RAC || train->wl_limit > RAIL_MAX_WL) {
        return false;
    }
    for (i = 0; i < train->station_count; ++i) {
        if (memchr(train->stations[i], '\0', RAIL_MAX_NAME) == NULL ||
            train->stations[i][0] == '\0') {
            return false;
        }
    }
    for (c = 0; c < RAIL_CLASS_COUNT; ++c) {
        if ((train->class_mask & (1U << c)) != 0 &&
            (train->coaches[c] == 0 || train->coaches[c] > RAIL_MAX_COACHES ||
             train->seats_per_coach[c] == 0 ||
             train->seats_per_coach[c] > RAIL_MAX_SEATS_PER_COACH)) {
            return false;
        }
    }
    return true;
}

/* Validate a decoded booking before restoration. O(PNR length). */
static bool valid_booking_record(const Booking *booking)
{
    size_t i;
    if (memchr(booking->pnr, '\0', sizeof(booking->pnr)) == NULL ||
        strlen(booking->pnr) != RAIL_PNR_LENGTH ||
        memchr(booking->passenger.name, '\0', sizeof(booking->passenger.name)) == NULL ||
        memchr(booking->passenger.phone, '\0', sizeof(booking->passenger.phone)) == NULL ||
        memchr(booking->passenger.berth_preference, '\0',
               sizeof(booking->passenger.berth_preference)) == NULL ||
        booking->train_number == 0 ||
        memchr(booking->journey_date, '\0', sizeof(booking->journey_date)) == NULL ||
        strlen(booking->journey_date) != RAIL_DATE_LENGTH - 1 ||
        memchr(booking->source, '\0', sizeof(booking->source)) == NULL ||
        memchr(booking->destination, '\0', sizeof(booking->destination)) == NULL ||
        booking->travel_class < RAIL_CLASS_SL ||
        booking->travel_class >= RAIL_CLASS_COUNT ||
        booking->status < RAIL_STATUS_CNF || booking->status > RAIL_STATUS_WL ||
        booking->quota < RAIL_QUOTA_GENERAL || booking->quota > RAIL_QUOTA_SENIOR) {
        return false;
    }
    if (booking->initial_status < RAIL_STATUS_CNF ||
        booking->initial_status > RAIL_STATUS_WL ||
        booking->passenger.age == 0 || booking->passenger.age > 120 ||
        (booking->passenger.gender != 'M' && booking->passenger.gender != 'F' &&
         booking->passenger.gender != 'O')) {
        return false;
    }
    for (i = 0; i < RAIL_PNR_LENGTH; ++i) {
        if (booking->pnr[i] < '0' || booking->pnr[i] > '9') {
            return false;
        }
    }
    return true;
}

/* Read every validated record and reject trailing bytes. O(records * insert). */
static RailError read_records(FILE *file, const StorageHeader *header,
                              SystemContext *temporary, bool trains)
{
    uint64_t i;
    if (trains) {
        for (i = 0; i < header->count; ++i) {
            Train train;
            if (fread(&train, sizeof(train), 1, file) != 1 ||
                !valid_train_record(&train) ||
                avl_find_train(temporary->train_root, train.number) != NULL ||
                engine_add_train(temporary, &train) != RAIL_OK) {
                return RAIL_ERR_CORRUPT;
            }
        }
    } else {
        for (i = 0; i < header->count; ++i) {
            Booking booking;
            RailError error;
            if (fread(&booking, sizeof(booking), 1, file) != 1 ||
                !valid_booking_record(&booking)) {
                return RAIL_ERR_CORRUPT;
            }
            error = engine_restore_booking(temporary, &booking);
            if (error != RAIL_OK) {
                return error == RAIL_ERR_MEMORY ? error : RAIL_ERR_CORRUPT;
            }
            if (strtoull(booking.pnr, NULL, 10) >= temporary->next_pnr) {
                temporary->next_pnr = strtoull(booking.pnr, NULL, 10) + 1;
            }
        }
    }
    return fgetc(file) == EOF && !ferror(file) ? RAIL_OK : RAIL_ERR_CORRUPT;
}

/* Load one file and preserve validation errors. O(records * insert). */
static RailError load_file(const char *path, const char *magic, uint64_t maximum,
                           SystemContext *temporary, bool trains)
{
    FILE *file = fopen(path, "rb");
    StorageHeader header;
    RailError error;
    if (file == NULL) {
        return errno == ENOENT ? RAIL_OK : RAIL_ERR_IO;
    }
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        !valid_header(&header, magic, maximum)) {
        (void)fclose(file);
        return RAIL_ERR_CORRUPT;
    }
    error = read_records(file, &header, temporary, trains);
    if (error == RAIL_OK) {
        if (header.next_pnr > temporary->next_pnr) {
            temporary->next_pnr = header.next_pnr;
        }
        if (header.next_timestamp > temporary->next_timestamp) {
            temporary->next_timestamp = header.next_timestamp;
        }
    }
    if (!close_checked(file, true)) {
        return RAIL_ERR_IO;
    }
    return error;
}

/* Load validated files atomically into an initialized empty context. */
RailError storage_load(SystemContext *context, const char *directory)
{
    char train_path[STORAGE_PATH_SIZE];
    char booking_path[STORAGE_PATH_SIZE];
    SystemContext temporary;
    RailError error;
    size_t notification_length;
    int notification_written;
    if (context == NULL || directory == NULL || context->train_root != NULL ||
        context->history_root != NULL || context->train_count != 0 ||
        context->booking_count != 0 ||
        context->trips != NULL) {
        return RAIL_ERR_INVALID;
    }
    if (!make_path(train_path, sizeof(train_path), directory, "trains.dat") ||
        !make_path(booking_path, sizeof(booking_path), directory, "bookings.dat")) {
        return RAIL_ERR_INVALID;
    }
    engine_init(&temporary);
    error = load_file(train_path, "RTRAIN1", RAIL_MAX_TRAINS, &temporary, true);
    if (error == RAIL_OK) {
        error = load_file(booking_path, "RBOOK01", RAIL_MAX_BOOKINGS,
                          &temporary, false);
    }
    if (error != RAIL_OK) {
        engine_destroy(&temporary);
        return error;
    }
    notification_length = strlen(directory);
    notification_written = snprintf(temporary.notification_path,
                                    sizeof(temporary.notification_path),
                                    "%s%snotifications.log", directory,
                                    notification_length > 0 &&
                                    (directory[notification_length - 1] == '/' ||
                                     directory[notification_length - 1] == '\\') ?
                                    "" : STORAGE_SEPARATOR);
    if (notification_written < 0 ||
        (size_t)notification_written >= sizeof(temporary.notification_path)) {
        engine_destroy(&temporary);
        return RAIL_ERR_INVALID;
    }
    *context = temporary;
    return RAIL_OK;
}
