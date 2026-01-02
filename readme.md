This project builds a SystemVerilog simulator with a compiler-style flow.

The goal is Verilator-like performance while keeping generated code stable and incremental-friendly.

Key ideas
- Use slang (https://sv-lang.com/) as the front-end parser and elaborator to cover a broad SV subset.
- Generate C++ from SV and run the simulation as a native binary.
- Split the build into two steps: build the generator once, then compile the generated C++.
- Keep a stable mapping from each SV source file to a corresponding C++ file to improve incremental builds
  when only a subset of SV files changes (Bazel-based incremental builds are not implemented yet).

Build and run
- Set the slang checkout path via the Makefile variable `SLANG_DIR`:
  - `make SLANG_DIR=/path/to/slang sim`
- Generate C++ and build the generated simulator:
  - `make SLANG_DIR=/path/to/slang gen`
  - `make SLANG_DIR=/path/to/slang gen_sim`
- Run the generated simulator:
  - `make SLANG_DIR=/path/to/slang run`

Makefile variables
- `SLANG_DIR`: absolute path to your slang checkout (headers and build outputs).
- `GEN_DIR`: output directory for generated C++ (default: `gen`).
- `TOP`: top module name passed to the generator (default: `adder_tb`).
- `FILELIST`: SV file list passed to the generator (default: `tests/file.f`).
