#include <stdio.h>
#include <string.h>

#include "ui.h"

static void add_demo_train(SystemContext *context)
{
    Train train;
    memset(&train, 0, sizeof(train));
    train.number = 12001;
    (void)snprintf(train.name, sizeof(train.name), "RailEase Express");
    train.station_count = 4;
    (void)snprintf(train.stations[0], RAIL_MAX_NAME, "Alpha");
    (void)snprintf(train.stations[1], RAIL_MAX_NAME, "Beta");
    (void)snprintf(train.stations[2], RAIL_MAX_NAME, "Gamma");
    (void)snprintf(train.stations[3], RAIL_MAX_NAME, "Delta");
    train.class_mask = 1U << RAIL_CLASS_SL;
    train.coaches[RAIL_CLASS_SL] = 1;
    train.seats_per_coach[RAIL_CLASS_SL] = 10;
    train.cancellation_rates[RAIL_CLASS_SL] =
        context->cancellation_rates[RAIL_CLASS_SL];
    train.rac_limit = 10;
    train.wl_limit = 20;
    if (engine_add_train(context, &train) != RAIL_OK) {
        (void)fprintf(stderr, "Could not initialize the demo train.\n");
    } else {
        (void)printf("Loaded sample train 12001 (Alpha-Beta-Gamma-Delta).\n");
    }
}

/* Load data, run the CLI, and release all resources. O(T+B) startup/shutdown. */
int main(void)
{
    SystemContext context;
    RailError error;
    engine_init(&context);
    error = storage_load(&context, "data");
    if (error != RAIL_OK) {
        (void)fprintf(stderr, "Stored data unavailable (%s); starting clean.\n",
                      rail_error_message(error));
    }
    if (context.train_root == NULL) {
        add_demo_train(&context);
    }
    ui_run(&context);
    engine_destroy(&context);
    return 0;
}
