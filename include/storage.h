#ifndef RAIL_EASE_STORAGE_H
#define RAIL_EASE_STORAGE_H

#include "engine.h"

/* Purpose: persist state. Params: context, directory. Returns error. O(T+B) I/O. */
RailError storage_save(SystemContext *context, const char *directory);

/* Purpose: validated atomic load. Params: empty context, directory. Returns error. O(T log T+B^2). */
RailError storage_load(SystemContext *context, const char *directory);

#endif
