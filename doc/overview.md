Project Overview

Purpose
- Build a minimal SystemVerilog compiler pipeline that emits C++ and runs a native simulation.
- Use slang for parsing and elaboration, then generate C++ per module plus a shared runtime.

What "minimal" means here
- Focus on the constructs used by the adder testbench.
- Prefer clarity and correctness over completeness.
- Generate readable C++ that mirrors the SV structure.

Supported features (initial)
- Modules, ports, and parameterized widths.
- Continuous assignments.
- `always_ff` with posedge/negedge sensitivity and nonblocking assignments.
- `initial` blocks with `#delay` and simple assignments.
- `$monitor` and `$finish`.

Code generation model
- Each SV source file generates a corresponding C++ source file.
- Each SV module definition becomes a C++ class.
- Each module instantiation becomes a C++ object.

Out of scope (initial)
- Full 4-state logic and full IEEE timing regions.
- `always_comb` / `always_latch`, assertions, DPI, classes, and interfaces.
