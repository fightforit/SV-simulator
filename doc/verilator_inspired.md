Verilator-Inspired Notes

Good Verilator-inspired pieces for the current structure

- Stable scheduling model: explicit active/NBA/time queues with deterministic order.
  Status: implemented (runtime uses time + enqueue order in `sim::Kernel`).
- Dependency-driven triggers: build per-process sensitivity from elaborated RHS signals and reschedule only on changes.
  Status: not yet implemented.
- Two-phase update for NBAs: stage nonblocking updates and commit in a single NBA phase.
  Status: implemented (NBA queue + apply phase).
- Separation of elaboration vs. runtime: resolve hierarchy and constants in the compiler stage; runtime stays generic.
  Status: partially implemented (compiler/runtime split exists; constant folding TBD).
- Constant folding and width inference: precompute constants and bit-widths in the IR for simpler generated C++.
  Status: not yet implemented.
- Signal packing model: store signals as packed bit-vectors with slice/concat helpers to reduce codegen complexity.
  Status: not yet implemented.
- Module class layout: one C++ class per module with ports, internals, and process methods.
  Status: implemented (codegen emits per-module C++).
- Fast eval entrypoints: emit per-process or per-module `eval()` methods and schedule those.
  Status: partially implemented (process callbacks exist; explicit eval entrypoints TBD).
- Reset handling conventions: explicit reset checks around sequential logic in generated code.
  Status: partially implemented (depends on `always_ff` codegen coverage).
- Traceability hooks: a simple trace interface for dumping signal changes (pre-VCD).
  Status: not yet implemented.
