Runtime Kernel Notes

Role
- Provide a shared C++ runtime that all generated modules link against.
- Own scheduling, signal storage, and system tasks.

Event Model
- Maintain a time-ordered queue for future events and a current-time active queue.
- At each simulation time, exhaust the active queue, then apply nonblocking updates.
- Use a stable scheduling order: events are ordered by time, then by enqueue order.

Queues
- Active queue: processes scheduled for the current time.
- NBA queue: staged nonblocking assignments applied after active work.
- Event queue: time-ordered future work, with a deterministic tie-breaker on enqueue order.

Scheduling Rules (minimal)
- When a signal changes, schedule all dependent processes.
- `always_ff` triggers on posedge/negedge of listed signals.
- Continuous assigns and `$monitor` triggers on any RHS change.
- `always_comb` should be scheduled when any RHS signal changes.

Signal Model
- Store a 2-state value (0/1) and bit width.
- Edge detection compares previous and current values.

Connectivity
- When modules are connected, propagate input/output signals into the trigger lists of
  dependent processes so cross-module changes trigger recomputation.

Limitations
- No inertial delays, transport delays, or 4-state resolution.
