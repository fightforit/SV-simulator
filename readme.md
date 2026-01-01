This project builds a SystemVerilog simulator with a compiler-style flow.

The goal is Verilator-like performance while keeping generated code stable and incremental-friendly.

Key ideas
- Use slang (https://sv-lang.com/) as the front-end parser and elaborator to cover a broad SV subset.
- Generate C++ from SV and run the simulation as a native binary.
- Split the build into two steps: build the generator once, then compile the generated C++.
- Keep a stable mapping from each SV source file to a corresponding C++ file to improve incremental builds
  (e.g., via Bazel) when only a subset of SV files changes.
