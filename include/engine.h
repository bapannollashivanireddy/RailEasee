#ifndef RAIL_EASE_ENGINE_H
#define RAIL_EASE_ENGINE_H

#include "avl.h"
#include "dll.h"
#include "heap.h"
#include "queue.h"
#include "segtree.h"
#include "stack.h"

typedef struct Trip {
    unsigned train_number;
    char date[RAIL_DATE_LENGTH];
    RailClass travel_class;
    unsigned segment_count;
    unsigned rac_limit;
    unsigned wl_limit;
    SeatBitmap seats;
    RacQueue rac;
    BookingHeap waiting;
    BookingRecord *waiting_view;
    struct Trip *next;
} Trip;

/* Purpose: initialize counters/defaults. Params: context. Returns nothing. O(1). */
void engine_init(SystemContext *context);

/* Purpose: free owned state. Params: context. Returns nothing. O(T+B+Q*S+H). */
void engine_destroy(SystemContext *context);

/* Purpose: add/update train. Params: context, config. Returns error. O(B+Q*S+log T). */
RailError engine_add_train(SystemContext *context, const Train *train);

/* Purpose: remove idle train. Params: context, number. Returns error. O(B+Q*S+log T). */
RailError engine_remove_train(SystemContext *context, unsigned train_number);

/* Purpose: reserve ticket. Params: passenger/journey/quota/output PNR. Returns error. O(B+Q+S+W+log W). */
RailError engine_book(SystemContext *context, const Passenger *passenger,
                      unsigned train_number, const char *date, const char *source,
                      const char *destination, RailClass travel_class,
                      QuotaType quota, char out_pnr[RAIL_PNR_LENGTH + 1]);

/* Purpose: restore persisted booking. Params: context, booking. Returns error. O(Q+S+R+W+log W+log H). */
RailError engine_restore_booking(SystemContext *context, const Booking *booking);

/* Purpose: cancel/reallocate. Params: context, PNR, optional refund. Returns error. O(B+Q+(R+W+1)*S+W+log H). */
RailError engine_cancel(SystemContext *context, const char *pnr,
                        double *refund);

/* Purpose: get status/position. Params: context, PNR, output booking/position. Returns error. Avg O(1)+O(Q+W). */
RailError engine_status(const SystemContext *context, const char *pnr,
                        Booking *booking, size_t *position);

/* Purpose: undo newest action. Params: context. Returns error. O(B+Q+(R+W+1)*S+W+log H). */
RailError engine_undo(SystemContext *context);

/* Purpose: find trip state. Params: context, train/date/class. Returns trip/NULL. O(Q). */
Trip *engine_find_trip(SystemContext *context, unsigned train_number,
                       const char *date, RailClass travel_class);

/* Purpose: resolve route segment range. Params: train/source/destination/output bounds. Returns error. O(stations). */
RailError engine_route_range(const Train *train, const char *source,
                             const char *destination, unsigned *from,
                             unsigned *to);

/* Purpose: validate YYYY-MM-DD. Params: date string. Returns validity. O(1). */
bool engine_valid_date(const char *date);

/* Purpose: append notification. Params: context, message. Returns error. O(message length). */
RailError engine_notify(const SystemContext *context, const char *message);

/* Purpose: display queues. Params: trip. Returns nothing. O(R+W). */
void engine_print_queues(const Trip *trip);

/* Purpose: demo cancellations. Params: context, count. Returns cancellations completed. O(N(B+Q+(R+W+1)*S+W)). */
size_t engine_simulate_cancellations(SystemContext *context, size_t count);

#endif
