Slang Integration Notes

Troubleshooting reference
- https://sv-lang.com/parsing.html

Parsing and elaboration
- Use `SyntaxTree::fromFile` to parse one or more input files.
- Add all trees to a single `Compilation`.
- Select the top instance named by `--top` from `compilation.getRoot().topInstances`.
- Optionally dump the parsed syntax trees to a JSON file for inspection.

AST dump status
- The current binary supports `--ast-out <path>` to emit a CST JSON array for the parsed files.

Current usage
- `./sim --top <top_module> -file <file.sv> [more.sv ...] --ast-out ast.json`
- `./sim --top <top_module> -file <filelist.f> --ast-out ast.json`
- `./sim --top <top_module> -file <file.sv> --cpp-out gen`
- `./sim --top <top_module> -file <file.sv> --cpp-out gen --no-sim`
- `-file` accepts multiple paths until the next flag; `.f` files list one path per line
  and ignore blank lines plus lines starting with `#` or `//`.

Build and run (generated C++)
- Build generator:
  - `g++ -std=c++20 src/main.cpp src/frontend.cpp src/simulator.cpp src/codegen.cpp src/runtime.cpp -Iinclude -I/home/chlu/slang/include -I/home/chlu/slang/build/source -I/home/chlu/slang/external -L/home/chlu/slang/build/lib -lsvlang -lfmt -lmimalloc -pthread -ldl -o sim`
- Generate C++:
  - `./sim --top adder_tb -file tests/adder.sv tests/adder_tb.sv --cpp-out gen --no-sim`
- Build generated sim:
  - `g++ -std=c++20 gen/sim_main.cpp src/runtime.cpp -Iinclude -o gen/sim`
- Run:
  - `./gen/sim`

Current status
- `--top <module>` is required; an empty input file list is an error.
- Missing instances/submodules are reported via slang diagnostics.

Discussion notes
- Module definitions map naturally to C++ classes; instances map to objects.
- `always_ff` is edge-triggered; `always_comb` is level-triggered from RHS signals.
- A minimal simulator uses an event loop with active, time, and NBA queues.
- Combinational loops can be detected best-effort at compile time via dependency cycles
  and guarded at runtime via delta-cycle iteration limits.
- The generator currently emits one C++ file per instantiated module under `--cpp-out`.
- The generator also writes `sim_main.cpp` in the output directory, which includes all emitted
  module `.cpp` files and runs `sim::Kernel`.
- The runtime now supports `$monitor`, `$finish`, and time-based scheduling for `initial` blocks.
- Generated code includes `initial` blocks with `#delay`, `forever` clocks, and monitor setup.
- Generated code instantiates child modules and wires ports based on the elaborated design.

IR extraction (compiler stage)
- Create a simulator IR that records:
  - Signals (name, width, storage class).
  - Continuous assignments (LHS, RHS expression tree).
  - Processes (`always_ff`, `always_comb`, `initial`) with sensitivity or timing.
  - Port connections (internal symbol alias to external signal).

Code generation
- Emit one C++ file per SV source file using a C++ generator (no Python).
- Translate each SV module definition into a C++ class.
- Translate each module instantiation into a C++ object.
- Emit a top-level C++ file that builds the instance hierarchy and starts the kernel.
- Emit or link a shared runtime library that provides scheduling and signal APIs.
