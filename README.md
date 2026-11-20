# RailEase

RailEase is a C11 command-line railway reservation demonstrator for an
Advanced Data Structures course. It models reservations by train, journey date,
class, and route segment. Confirmed-seat cancellations trigger immediate RAC
and waiting-list promotions.

The sample train (12001, Alpha-Beta-Gamma-Delta) is added when no saved train
directory exists. It has ten SL seats, ten RAC places, and twenty waiting-list
places, making the reallocation demo immediately usable.

## Data structures and feature mapping

| Structure | RailEase use | Implementation |
|---|---|---|
| Chained hash table | PNR lookup, insertion, cancellation | `hashtable.c`; load-factor-resized, average O(1), worst O(B) |
| Indexed min-heap | Waiting-list priority and arbitrary removal | `heap.c`; insert/remove O(log W), peek O(1) |
| Circular queue | FIFO RAC allocation | `queue.c`; enqueue/dequeue O(1), known-item removal O(R) |
| Doubly linked list | Ordered live waiting-list view | `dll.c`; position lookup O(W), removal O(1) |
| AVL tree | Train directory and date-keyed booking history | `avl.c`; lookup/insert/delete O(log N), traversal O(N) |
| Seat bitmap | Per-seat occupancy on journey segments | `segtree.c`; range test/update O(1), seat search O(S) |
| Linked stack | Last booking/cancellation undo snapshots | `stack.c`; push/pop O(1) |

`SystemContext` owns the trees, chained hash table, trip states, notification
path, counters, and undo stack. There is no mutable file-scope application
state. Train routes are limited to 16 stations and a seat bitmap uses a 16-bit
mask, one bit per segment.

## Complexity notes

Let T be trains, B active bookings, S seats in the selected class, R RAC
passengers, and W waiting-list passengers.

| Operation | Complexity |
|---|---|
| PNR lookup/insert/delete | Average O(1), worst O(B); insertion is amortized O(1) with occasional O(B) resize |
| Train lookup/insert/delete | O(log T) |
| Allocate a confirmed seat | O(S) search plus O(1) segment update |
| RAC enqueue/dequeue | O(1) |
| Waiting-list insertion/removal | O(log W) heap work; O(W) ordered-view insertion |
| Waiting-list live position refresh | O(W) |
| Cancellation and reallocation | O(B + Q + (R+W+1)*S + W) worst case |
| Queue display | O(R + W) |
| Estimate probability | O(B + S) |
| Save/load | O(T + B) record I/O; worst-case O(T log T + B^2) to restore bookings |

The ordered DLL insertion is linear because it keeps the display view aligned
with quota and booking timestamp; the heap remains the authority for selecting
the next waiting passenger. The engine scans RAC entries to find the first
passenger whose route can use a released seat, since seat availability differs
by segment. Q denotes trip states and H denotes history entries; each engine
lookup of a train/date/class trip currently scans the trip list.

The probability estimator uses expected cancellations
`lambda = class_rate * confirmed_bookings * min(days_to_departure / 30, 1)`.
It adds free seats on the passenger's exact segment range to form a capacity
budget and reports `100 * budget / (budget + live_queue_position)`, labeled
High at 67% or above, Medium at 34% or above, and Low otherwise.

## Build and run

With GCC and GNU Make installed:

```sh
make
make run
make test
make clean
```

The Makefile uses `-Wall -Wextra -Wpedantic -std=c11`. The source uses only the
C standard library and runs on MinGW and Linux. `make test` prints PASS/FAIL
for the hash table, heap, AVL tree, auxiliary structures, booking/reallocation
integration case, and the 10,000-operation stress case.

## Browser website

Open `web/index.html` directly in a modern browser, or serve the project root
with `python -m http.server 8000` and visit `http://localhost:8000/web/`.
No package installation or external assets are required. The responsive site
includes train search, passenger booking, PNR status and cancellation,
confirmation estimates, segment-aware seat reuse, RAC/WL promotion, undo,
train administration, a cancellation simulation, and notification-log export.

The website is a standalone front-end demo: it stores its data in this
browser's local storage and does not connect to the C executable or its binary
files. The C CLI remains the authoritative implementation of the ADS project.

## Sample session

```text
Loaded sample train 12001 (Alpha-Beta-Gamma-Delta).
========== RailEase ==========
1. Search trains       2. Book ticket
3. Cancel ticket       4. PNR status
5. Confirmation chance 6. Train occupancy
7. Undo last action    8. Admin menu
9. Simulate N cancellations  0. Save and exit
Select: 2
Passenger name: Arun Kumar
...
PNR 1000000000 status changed to CNF
Booked PNR 1000000000: CNF seat 1
RAC (0):
WL (0):
...
Select: 0
Data saved. Goodbye.
```

## Reallocation demo (menu option 9)

For the full sequence, book ten passengers on 12001 in SL for the same date
and route to occupy all seats, then ten more to fill RAC, then twenty to fill
WL. Use option 9 and enter `5`. The first five eligible RAC passengers are
promoted to CNF; the highest-priority five WL passengers fill the resulting
RAC places. Promotions and resulting queue views are shown, and status changes
are appended to `data/notifications.log`.

The engine also exposes `engine_simulate_cancellations()` for repeatable
programmatic demonstrations. The test integration case asserts the exact
promotion order after five confirmed cancellations.

## Persistence

On startup RailEase reads versioned binary `data/trains.dat` and
`data/bookings.dat`. Missing files are treated as an empty database. Invalid
headers, impossible counts, truncated records, invalid field values, or
trailing bytes are rejected; the application reports the issue and starts
clean rather than using partial data. Option 0 saves both files. Notifications
are appended to `data/notifications.log`.

Binary record files are intended for this C program built with the same
compiler/ABI; they are not a portable interchange format between arbitrary
architectures or compiler versions.

## Known limitations

* The CLI treats a journey date as an operating date; it does not model
  departure times, schedules, fare tables, or coach/berth-specific preferences.
* The refund estimate uses a documented example fare per class and calendar
  date: 100% more than two days before departure, 75% more than one day, and
  50% otherwise. It is not a real railway tariff.
* Confirmation probability is a heuristic based on the configured historical
  class cancellation rate, confirmed bookings, time factor, available seats,
  and current queue rank. Default cancellation rates are illustrative and
  should be calibrated from real history. The admin train form stores a
  cancellation percentage separately for each enabled class.
* The quota order is GENERAL, TATKAL, LADIES, SENIOR, then timestamp. This is
  deliberately explicit and configurable in `heap.c`, not a claim about a
  real railway's rules.
* Undo restores the last direct action. If a canceled confirmed seat has been
  consumed by later reallocation, restoring that cancellation degrades it to
  RAC or WL rather than displacing an already-promoted passenger.
* This is a local educational demo: no user authentication, concurrent access,
  payment processing, or crash-safe multi-file transaction protocol is
  provided.
