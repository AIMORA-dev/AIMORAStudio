# AIMORAStudio

`AIMORAStudio` is the public native desktop client for AIMORA. The accepted first-release architecture is C++20, Qt 6 Widgets, CMake, and an out-of-process Julia service. The product target combines semantic power-system editing and study integration with a precise AutoCAD-like single-line-diagram and engineering-drawing workflow while keeping Julia as the only engineering source of truth.

## Current status

GUI020 installs the native repository foundation. It provides a real C++20/CMake workspace, exact Qt dependency lock, foundational libraries, a native command-line smoke executable, unit and structural tests, formatting/static-analysis/sanitizer configuration, install/export rules, and native Windows/macOS/Linux CI.

GUI020 does **not** yet implement the visible graphical shell, Julia process lifecycle, renderer, semantic drawing model, property inspector, CAD tools, PDF plotting, or DXF exchange. Those remain in later packets.

## Frozen desktop architecture

```text
AIMORAStudio
    C++20
    Qt 6.11.2 Widgets
    CMake 3.28 or newer
    native Windows, macOS, and Linux application
            |
            | authenticated local socket or named pipe
            v
AIMORAService.jl
    canonical project, revisions, transactions, schemas, jobs, and artifacts
            |
            | bounded worker protocol
            v
AIMORA study worker
    AIMORA.jl and AIMORASolvers.jl numerical execution
```

The primary desktop application has no Electron, Chromium, Node.js runtime, Tauri, Rust, React, TypeScript, browser webview, Qt Quick, or QML dependency. Julia is not embedded into the GUI process for the first release.

## Repository layout

```text
apps/studio/           Native application target
packages/core/         Product/version and common native foundation
packages/protocol/     Bounded generated-client configuration boundary
packages/canvas/       Renderer-neutral viewport foundation
packages/inspector/    Native panel-state foundation
packages/commands/     Deterministic command registry foundation
packages/themes/       Dark/light/system mode foundation
cmake/                 Warnings, analysis, sanitizers, install, and contracts
tests/                 Native unit and source-structure tests
dependencies/          Exact Qt/tooling and licence inventory
```

No package in this repository owns physical equipment equations, topology, units, readiness, study calculations, result validity, or full project state.

## Build

Prerequisites:

- CMake 3.28 or newer;
- a C++20 compiler supported by the selected Qt build;
- Qt 6.11.2 with Core, Gui, Widgets, Network, PrintSupport, Concurrent, and Test for test builds;
- Ninja for the supplied local presets, or another CMake generator when configuring manually.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

A generator-neutral manual build is also supported:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --build build --target aimora_studio_smoke
```

The smoke executable is intentionally non-graphical in GUI020:

```bash
./build/apps/studio/aimora-studio --architecture
```

## Quality gates

The repository includes:

- strict GCC, Clang, and MSVC warnings;
- optional warnings-as-errors;
- `.clang-format` policy, deterministic whitespace/line-length checks, and `.clang-tidy` analysis;
- AddressSanitizer and UndefinedBehaviorSanitizer configuration;
- Qt Test unit coverage of every foundational package;
- a CMake source-tree contract that rejects the retired TypeScript/Node scaffold and prohibited primary-client file types;
- install/export and archive-package foundations;
- native CI for Windows, macOS, and Linux.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the frozen ownership and future product boundary.

## Licence

This repository's AIMORA-authored content is distributed under the PolyForm Noncommercial License 1.0.0. Research, education, personal study, public-interest noncommercial use, and other purposes permitted by that licence are free; commercial use requires a separate written agreement with Ahmed Elkholy <ahmed_elkholy@f-eng.tanta.edu.eg>. There is no licence key, activation, telemetry, or technical feature restriction. Clearly identified third-party material retains its own terms, and copies received under an earlier licence retain those prior grants.

Qt is an independent third-party dependency. Each distributed build must record the exact Qt edition, version, modules, licence route, notices, source/relinking obligations where applicable, and dependency inventory. This repository does not provide legal advice.
