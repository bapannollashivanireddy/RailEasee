#include <string.h>

#include "probability.h"

/* Clamp a score to percentage bounds. O(1) time and O(1) space. */
static double clamp_percent(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    return value > 100.0 ? 100.0 : value;
}

/* Estimate confirmation chance using expected promotions. O(B+S) time, O(1) space.
 * Expected cancellations = class rate * confirmed bookings * min(days/30, 1).
 * Remaining free seats reduce the number of promotions needed; the returned
 * score is 100 * capacity_budget / (capacity_budget + live queue position).
 */
ConfirmationEstimate probability_estimate(const SystemContext *context,
                                          const Trip *trip,
                                          const Booking *booking,
                                          unsigned days_to_departure)
{
    ConfirmationEstimate estimate = {0.0, "Low"};
    size_t i;
    size_t free_seats = 0;
    size_t confirmed = 0;
    const TrainNode *train_node;
    size_t position;
    unsigned from;
    unsigned to;
    double time_factor;
    double expected_cancellations;
    double budget;
    if (context == NULL || trip == NULL || booking == NULL ||
        booking->travel_class < RAIL_CLASS_SL ||
        booking->travel_class >= RAIL_CLASS_COUNT ||
        booking->status == RAIL_STATUS_CNF) {
        return estimate;
    }
    train_node = avl_find_train(context->train_root, booking->train_number);
    if (train_node == NULL ||
        engine_route_range(&train_node->train, booking->source,
                           booking->destination, &from, &to) != RAIL_OK) {
        return estimate;
    }
    position = booking->queue_position;
    if (position == 0) {
        return estimate;
    }
    for (i = 0; i < trip->seats.seat_count; ++i) {
        if (seat_bitmap_available(&trip->seats, i, from, to)) {
            ++free_seats;
        }
    }
    for (i = 0; i < context->booking_bucket_count; ++i) {
        const BookingRecord *record;
        for (record = context->booking_buckets[i]; record != NULL;
             record = record->hash_next) {
            if (record->booking.train_number == booking->train_number &&
                record->booking.travel_class == booking->travel_class &&
                strcmp(record->booking.journey_date, booking->journey_date) == 0 &&
                record->booking.status == RAIL_STATUS_CNF) {
                ++confirmed;
            }
        }
    }
    time_factor = (double)days_to_departure / 30.0;
    if (time_factor > 1.0) {
        time_factor = 1.0;
    }
    expected_cancellations =
        train_node->train.cancellation_rates[booking->travel_class] *
        (double)confirmed * time_factor;
    budget = (double)free_seats + expected_cancellations;
    estimate.percentage = clamp_percent(100.0 * budget /
                                        (budget + (double)position));
    if (estimate.percentage >= 67.0) {
        estimate.label = "High";
    } else if (estimate.percentage >= 34.0) {
        estimate.label = "Medium";
    }
    return estimate;
}
