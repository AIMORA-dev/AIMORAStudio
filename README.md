# AIMORAStudio

`AIMORAStudio` is the public native desktop client for AIMORA. The accepted first-release architecture is C++20, Qt 6.11.2 Widgets, CMake, and an out-of-process Julia engineering service.

The product target combines semantic power-system editing and study integration with a precise AutoCAD-like single-line-diagram and engineering-drawing workflow while keeping Julia as the only engineering source of truth.

## Current status

GUI030 delivers the first visible native application shell:

- native `QApplication` and `QMainWindow`;
- drawing-first workspace with only the menubar visible by default;
- File, Edit, View, Draw, Modify, Electrical, Studies, Results, Output, Tools, and Help menus;
- hidden-on-start Project Browser, Inspector, and Command Line dock panels;
- dock, float, pin, hide, restore, reset, and corrupt-layout recovery behavior;
- persisted `system`, `light`, and `dark` appearance modes;
- semantic theme tokens with committed light/dark fixtures and contrast checks;
- high-DPI pass-through policy;
- native About dialog;
- offscreen shell smoke tests and Qt Test automation.

GUI030 does not yet connect to Julia, render semantic SLD objects, edit equipment parameters, execute CAD commands, plot engineering PDF, or exchange DXF. Those capabilities remain in later dependency-ordered packets.

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
apps/studio/           Native application target and startup policy
packages/core/         Product/version and common native foundation
packages/protocol/     Bounded generated-client configuration boundary
packages/canvas/       Renderer-neutral viewport foundation
packages/inspector/    Native panel-state foundation
packages/commands/     Deterministic command registry foundation
packages/themes/       Theme modes, tokens, palette, persistence, controller
packages/shell/        Main window, menus, drawing surface, docks, layout state
cmake/                 Warnings, analysis, sanitizers, install, contracts
tests/                 Native unit, shell, fixture, and source-structure tests
dependencies/          Exact Qt/tooling and licence inventory
```

No package in this repository owns physical equipment equations, topology, units, readiness, study calculations, result validity, or full project state.

## Build

Prerequisites:

- CMake 3.28 or newer;
- a C++20 compiler supported by the selected Qt build;
- Qt 6.11.2 with Core, Gui, Widgets, Network, PrintSupport, Concurrent, and Test;
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
```

Run the application:

```bash
./build/apps/studio/aimora-studio
```

Run the committed light/dark shell smoke target:

```bash
cmake --build build --target aimora_studio_smoke
```

Useful command-line options:

```text
--theme system|light|dark
--reset-workspace
--windowed
--shell-smoke
--architecture
--version
```

## Shell behavior

The first launch opens maximized unless a valid saved workspace exists or `--windowed` is supplied. Only the menubar and central drawing surface are visible initially.

Use **View** to show:

- Project Browser — `Ctrl+Shift+E`;
- Inspector — `F4`;
- Command Line — `Ctrl+9`.

Each panel can dock, float, hide, or be pinned through its native context action. Window geometry, dock state, and pin state are user settings and never enter the engineering project or physical hash.

The View menu also selects system, light, or dark appearance and can reset corrupted or unwanted workspace layout. Screen appearance is independent of future paper-space, plot-style, PDF, and DXF semantics.

## Quality gates

The repository includes:

- strict GCC, Clang, and MSVC warnings;
- optional warnings-as-errors;
- `.clang-format` and `.clang-tidy`;
- AddressSanitizer and UndefinedBehaviorSanitizer configuration;
- Qt Test coverage of foundation, themes, menus, docks, persistence, recovery, and rendering;
- committed semantic light/dark fixtures;
- CMake source-tree, formatting, and fixture verification scripts;
- installation/export and archive-package foundations;
- native CI definitions for Windows, macOS, and Linux.

No implementation script or source used by GUI030 is kept only in a temporary directory. The application, CMake verification scripts, fixtures, tests, and CI definition are tracked in this repository.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the frozen ownership and future product boundary.

## Licence

This repository's AIMORA-authored content is distributed under the PolyForm Noncommercial License 1.0.0. Research, education, personal study, public-interest noncommercial use, and other purposes permitted by that licence are free; commercial use requires a separate written agreement with Ahmed Elkholy <ahmed_elkholy@f-eng.tanta.edu.eg>. There is no licence key, activation, telemetry, or technical feature restriction. Clearly identified third-party material retains its own terms, and copies received under an earlier licence retain those prior grants.

Qt is an independent third-party dependency. Each distributed build must record the exact Qt edition, version, modules, licence route, notices, source/relinking obligations where applicable, and dependency inventory. This repository does not provide legal advice.
