Milestones and Validation

Milestone 1: Define the supported SV subset
- Document the supported constructs, types, and timing semantics.
- Treat provided SV files as tests, not special cases.

Milestone 2: Simulator IR
- Define a general IR for modules, ports, signals, expressions, statements, and processes.
- Map the supported SV subset to the IR.

Milestone 3: Frontend (slang) extraction
- Parse and elaborate arbitrary SV designs within the supported subset.
- Extract the IR without hardcoded module names or file assumptions.

Status update
- AST JSON dumping is working via `--ast-out <path>` in the current `sim` binary.
- `--top <module>` is required and missing input files now emit an error.
- Missing instances/submodules are reported through slang diagnostics.
- `-file` accepts multiple SV paths and `.f` file lists.
- C++ stubs can be generated per instantiated module using `--cpp-out <dir>`.
- Runtime library skeleton added (`sim::Kernel`, `sim::Signal`, NBA queue, edge scheduling).
- Codegen now emits `sim_main.cpp` to run the generated modules with the runtime.
- Runtime now supports `$monitor`, `$finish`, and time-based scheduling.
- Codegen now emits `initial` blocks with `#delay`, `forever` clocks, and monitor setup.
- Generated code compiles and links with `src/runtime.cpp`.
- Codegen now instantiates child modules and wires ports (e.g., `adder_tb` instantiates `adder`).

Milestone 4: Runtime kernel
- Implement the shared runtime library (scheduler, signals, NBA queue).
- Provide a minimal system task API for `$monitor` and `$finish`.

Milestone 5: Code generation
- Emit C++ per module and a top-level driver file.
- Compile the generated sources and run the simulation binary.

Validation
- Run the generated binaries for multiple SV inputs, including `tests/adder.sv` and `tests/adder_tb.sv`,
  and confirm observable behavior matches expectations for the supported subset.
