# Slang-Based SV-to-C++ Simulator Compiler

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

This repository includes `doc/PLANS.md`, which defines ExecPlan requirements. This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

The goal is to provide a compiler-style SystemVerilog simulator: parse and elaborate `tests/adder.sv` and `tests/adder_tb.sv` with slang, translate them into generated C++ files, compile the C++ into a native binary, and run it to observe `$monitor` output and `$finish` termination. After completion, a user can build the generated C++ and run the simulation without an external SV simulator.

## Progress

- [x] (2025-12-31 17:31Z) Moved all markdown files into `doc/` and consolidated planning into this ExecPlan.
- [x] (2025-12-31 17:49Z) Updated documentation to describe the compiler-style SV-to-C++ flow.
- [ ] Define the simulator IR types and their mapping to generated C++.
- [ ] Implement slang-based extraction from AST to IR (signals, assigns, processes, connections).
- [ ] Implement code generation for module classes and instance wiring.
- [ ] Implement the runtime kernel library (scheduler, signals, NBA queue).
- [ ] Implement system tasks in the runtime (`$monitor`, `$finish`).
- [ ] Implement the generated top-level driver (build hierarchy, start kernel).
- [ ] Validate by compiling generated C++ and comparing `$monitor` output to expected sums.

## Surprises & Discoveries

- None yet.

## Decision Log

- Decision: Use slang as the sole parser and elaborator, then interpret a narrow subset of SV directly from the elaborated AST.
  Rationale: It minimized scope for initial exploration.
  Date/Author: 2025-12-31, Codex

- Decision: Switch to a compiler-style flow that emits C++ per SV file plus a shared runtime.
  Rationale: The user wants a conventional C++ simulation binary produced from SV sources.
  Date/Author: 2025-12-31, Codex

## Outcomes & Retrospective

- Not started yet.

## Context and Orientation

The repository root contains `tests/adder.sv` and `tests/adder_tb.sv`, which define a parameterized adder and a simple testbench. The C++ simulator will be organized as a larger project with a shared runtime library and generated sources. Documentation is under `doc/` and includes `doc/overview.md`, `doc/kernel.md`, `doc/slang_integration.md`, and `doc/milestones.md`.

Key dependency: slang is installed under `/home/chlu/slang` with headers in `/home/chlu/slang/include` and libraries in `/home/chlu/slang/build/lib` (notably `libsvlang.a`).

## Plan of Work

Create a clean project layout with a handwritten runtime library and generated C++ sources. The runtime library should expose a scheduler, signal storage, and system task hooks, and be compiled as a static library. The compiler stage should parse and elaborate the SV sources using slang in C++, extract a simulator IR (signals, assigns, processes, and connections), then emit C++ per module plus a generated top-level driver that instantiates the hierarchy and starts the scheduler. Keep the generated code under a `gen/` directory and handwritten runtime code under `runtime/`.

Breakdown
- Define the IR in C++ (structs for modules, signals, processes, expressions, statements).
- Build an AST-to-IR extractor using slang symbols and expressions.
- Implement codegen for:
  - Module classes and member signals.
  - Process bodies and sensitivity registration.
  - Instance creation and port wiring.
- Implement runtime kernel (scheduler, signals, NBA queue, system tasks).
- Generate a top-level driver that constructs the hierarchy and starts simulation.
- Build and validate against `adder_tb`.

## Concrete Steps

Run the following from `/home/chlu/vsim` once the generator and runtime are implemented:

    g++ -std=c++20 src/main.cpp src/frontend.cpp src/simulator.cpp src/codegen.cpp src/runtime.cpp -Iinclude \
      -I/home/chlu/slang/include -I/home/chlu/slang/build/source -I/home/chlu/slang/external \
      -L/home/chlu/slang/build/lib -lsvlang -lfmt -lmimalloc -pthread -ldl -o sim

    ./sim --top adder_tb -file tests/file.f --cpp-out gen --no-sim

    g++ -std=c++20 gen/sim_main.cpp src/runtime.cpp -Iinclude -o gen/sim

    ./gen/sim

If compilation fails due to missing libraries, verify the slang build output under `/home/chlu/slang/build/lib` and update the library flags accordingly.

## Validation and Acceptance

Acceptance is achieved when running `./sim` prints `$monitor` lines that show `sum` matching `a + b` after reset is released, and the simulation terminates at `$finish` without errors. The output should include time stamps and the values of `rstn`, `a`, `b`, and `sum` as specified by the testbench format string.

## Idempotence and Recovery

Generation and builds should be repeatable. Re-running the generator should overwrite `gen/` safely. If the simulator crashes or prints incorrect values, adjust the generator or runtime and repeat the build and run steps.

## Artifacts and Notes

No build or run transcripts recorded yet.

## Interfaces and Dependencies

The compiler and runtime depend on slang headers from `/home/chlu/slang/include` and the slang static library `libsvlang.a` in `/home/chlu/slang/build/lib`. The runtime should provide a stable API for generated code, including:
- Signal creation and storage, with bit width and value.
- Scheduling hooks for edge-triggered and level-sensitive processes.
- Nonblocking assignment staging and application.
- System task entry points for `$monitor` and `$finish`.

Change Note: Updated this ExecPlan to reflect the compiler-style SV-to-C++ flow and the new project layout with generated sources and a shared runtime.
