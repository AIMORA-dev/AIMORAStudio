# Contributing

AIMORAStudio contributions target the accepted native C++20/Qt 6 desktop architecture. Engineering semantics remain in canonical Julia owners, and generated C++ client types come from the public versioned service schema.

## Build and test before submitting

Use Qt 6.11.2 and CMake 3.28 or newer.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Before a commit, run every committed repository contract:

```bash
cmake -DAIMORA_SOURCE_DIR="$PWD" -P cmake/VerifySourceTree.cmake
cmake -DAIMORA_SOURCE_DIR="$PWD" -P cmake/VerifyThemeFixtures.cmake
cmake -DAIMORA_SOURCE_DIR="$PWD" -P cmake/VerifyFormatting.cmake
cmake --build build/dev --target aimora_studio_smoke
```

Run the warnings/static-analysis preset:

```bash
cmake --preset quality
cmake --build --preset quality
ctest --preset quality
```

Use the sanitizer preset on a supported Clang or GCC environment:

```bash
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

All implementation source, CMake scripts, fixtures, and tests required to reproduce a packet must be tracked in the repository. Temporary generation files may assist development but cannot be the only copy of accepted work.

## Required boundaries

- Do not add physical equipment equations, topology rules, units, readiness logic, or study calculations to C++.
- Do not add Electron, Chromium, Node.js runtime, Tauri, Rust, React, TypeScript, browser webviews, Qt Quick, or QML to the primary desktop product.
- Do not create one widget, object, or graphics item per drawing entity.
- Do not infer electrical connectivity from geometry or imported CAD lines.
- Do not make SVG the canonical symbol, scene, drawing, or publication format.
- Do not expose private solver types, paths, source, or evidence through the client protocol.
- Do not add a permanent ribbon, toolbar, status bar, or always-visible sidebar to the default drawing workspace.
- Do not make screen theme tokens control paper color, engineering lineweights, PDF, or DXF output.

## Package ownership

- `core` owns application identity and common native primitives only.
- `protocol` owns generated-client transport/configuration surfaces, never Julia schema authority.
- `canvas` owns renderer-neutral view and interaction state, never engineering topology.
- `inspector` owns native presentation state, never equipment definitions.
- `commands` owns client command registration and routing metadata, never Julia mutation rules.
- `themes` owns screen modes, semantic colors, palette application, and preference persistence.
- `shell` owns `QMainWindow`, menus, native docks, clean-workspace behavior, and local layout state.
- `apps/studio` composes packages and must not absorb package-owned behavior.

## Shell and theme rules

The default shell must show only the native menubar and central drawing surface. Panels start hidden and must remain optional, dockable, floatable, pinnable, and recoverable from invalid saved state.

Menu families are stable: File, Edit, View, Draw, Modify, Electrical, Studies, Results, Output, Tools, and Help. Commands not implemented by the current accepted capability must be disabled and explained, not simulated.

Theme changes use semantic tokens and keep dark/light feature parity. New tokens require:

- committed light and dark fixture values;
- contrast checks;
- palette and drawing-surface tests;
- high-DPI review;
- proof that publication semantics are unchanged.

## C++ and Qt quality

Use C++20, RAII, explicit ownership, value types, deterministic cleanup, checked protocol boundaries, and no owning raw pointers. New code must pass formatting, compiler warnings, static analysis, sanitizers, unit tests, interaction tests, resource checks, and native Windows/macOS/Linux CI as required by its packet.

Qt dependencies must remain within the accepted module and licence boundary. A new Qt or third-party dependency requires an executable consumer, resource measurement, licence inventory, and explicit packet authorization.

All C++ and CMake source files must carry the repository SPDX identifier. Keep public headers minimal, deterministic, and free of private solver or Qt private/RHI types outside the isolated renderer package authorized for GUI080.

## Symbols and assets

AIMORA symbols and UI artwork must be original or have exact compatible redistribution and commercial-use rights. Familiar engineering conventions may be implemented independently, but proprietary ETAP, DIgSILENT, AutoCAD, manufacturer, standards-body, logo, font, or drawing assets must not be copied without permission and provenance.

## Commit policy

Each packet creates at most one implementation commit in each changed child repository. The parent Workspace may require one separate coordination commit to record the reviewed child Gitlink and packet state because Git cannot commit across independent repositories atomically. Intermediate, progress, temporary-script, or cleanup commits are not accepted packet output.

## Contribution licence

By submitting a contribution, you certify that you have the right to provide it and agree that you retain your copyright while granting Ahmed Elkholy a perpetual, worldwide, irrevocable, nonexclusive, royalty-free right to use, reproduce, modify, distribute, sublicense, and offer the contribution under this repository's PolyForm Noncommercial terms and under separately negotiated commercial agreements. Clearly identified third-party material is accepted only with compatible written redistribution and commercial-use rights; submitting it does not override its original licence.
