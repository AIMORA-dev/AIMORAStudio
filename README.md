# AIMORAStudio

`AIMORAStudio` is the public native desktop client for AIMORA. The accepted first-release architecture is C++20, Qt 6 Widgets, CMake, and an out-of-process Julia service. The application is intended to combine semantic power-system editing and study integration with a precise AutoCAD-like single-line-diagram and engineering-drawing workflow while keeping Julia as the only engineering source of truth.

## Current status

The repository still contains the earlier minimal TypeScript protocol scaffold. It is retained only as migration history and does not define the accepted desktop architecture. `GUI020` will replace that scaffold with the native C++/Qt workspace after equivalent structural checks exist. No production GUI capability is claimed by this documentation-only architecture freeze.

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

## Studio responsibilities

- Native menu-only shell with the remaining window dedicated to the drawing canvas.
- On-demand, dockable, floating, pinnable, and hideable inspectors and result panels.
- High-performance retained SLD/CAD scene, local interaction previews, snapping, selection, and commands.
- Dark, light, and system themes with identical functionality and print-independent styles.
- Schema-driven property editing through stable Julia asset IDs.
- Native print preview, engineering sheets, vector PDF requests, and DXF workflows.
- Public unit, interaction, renderer, accessibility, memory, performance, packaging, and cross-platform tests.

## Studio does not own

- Physical equipment classes, topology, units, ratings, model readiness, or study equations.
- Canonical drawing, sheet, symbol, result, or report semantics.
- Solver state, full-resolution waveforms, sparse matrices, or private solver types.
- Automatic inference of electrical connectivity from graphical intersections or imported CAD geometry.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the complete frozen boundary.

## Licence

This repository's AIMORA-authored content is distributed under the PolyForm Noncommercial License 1.0.0. Research, education, personal study, public-interest noncommercial use, and other purposes permitted by that licence are free; commercial use requires a separate written agreement with Ahmed Elkholy <ahmed_elkholy@f-eng.tanta.edu.eg>. There is no licence key, activation, telemetry, or technical feature restriction. Clearly identified third-party material retains its own terms, and copies received under an earlier licence retain those prior grants.

Qt is an independent third-party dependency. Each distributed build must record the exact Qt edition, version, modules, licence route, notices, source/relinking obligations where applicable, and dependency inventory. This repository does not provide legal advice.