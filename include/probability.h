#ifndef RAIL_EASE_PROBABILITY_H
#define RAIL_EASE_PROBABILITY_H

#include "engine.h"

typedef struct {
    double percentage;
    const char *label;
} ConfirmationEstimate;

/* Purpose: estimate current chance. Params: context, trip, live booking, days. Returns score/label. O(B+S). */
ConfirmationEstimate probability_estimate(const SystemContext *context,
                                          const Trip *trip,
                                          const Booking *booking,
                                          unsigned days_to_departure);

#endif
