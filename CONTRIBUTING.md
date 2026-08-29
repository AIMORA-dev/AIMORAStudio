# Contributing

AIMORAStudio contributions target the accepted native C++20/Qt 6 desktop architecture. Engineering semantics remain in canonical Julia owners, and generated C++ client types come from the public versioned service schema.

## Build and test before submitting

Use Qt 6.11.2 and CMake 3.28 or newer.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Before a commit, also run the source-tree contract and a warnings-as-errors build:

```bash
cmake -DAIMORA_SOURCE_DIR="$PWD" -P cmake/VerifySourceTree.cmake
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

## Required boundaries

- Do not add physical equipment equations, topology rules, units, readiness logic, or study calculations to C++.
- Do not add Electron, Chromium, Node.js runtime, Tauri, Rust, React, TypeScript, browser webviews, Qt Quick, or QML to the primary desktop product.
- Do not create one widget, object, or graphics item per drawing entity.
- Do not infer electrical connectivity from geometry or imported CAD lines.
- Do not make SVG the canonical symbol, scene, drawing, or publication format.
- Do not expose private solver types, paths, source, or evidence through the client protocol.

## Package ownership

- `core` owns application identity and common native primitives only.
- `protocol` owns generated-client transport/configuration surfaces, never Julia schema authority.
- `canvas` owns renderer-neutral view and interaction state, never engineering topology.
- `inspector` owns native presentation state, never equipment definitions.
- `commands` owns client command registration and routing metadata, never Julia mutation rules.
- `themes` owns screen theme state, never engineering print styles.
- `apps/studio` composes packages and must not absorb package-owned behavior.

## C++ and Qt quality

Use C++20, RAII, explicit ownership, value types, deterministic cleanup, checked protocol boundaries, and no owning raw pointers. New code must pass formatting, compiler warnings, static analysis, sanitizers, unit tests, interaction tests, resource checks, and native Windows/macOS/Linux CI as required by its packet.

Qt dependencies must remain within the accepted module and licence boundary. A new Qt or third-party dependency requires an executable consumer, resource measurement, licence inventory, and explicit packet authorization.

All C++ and CMake source files must carry the repository SPDX identifier. Keep public headers minimal, deterministic, and free of private solver or Qt private/RHI types outside the isolated renderer package authorized for GUI080.

## Symbols and assets

AIMORA symbols and UI artwork must be original or have exact compatible redistribution and commercial-use rights. Familiar engineering conventions may be implemented independently, but proprietary ETAP, DIgSILENT, AutoCAD, manufacturer, standards-body, logo, font, or drawing assets must not be copied without permission and provenance.

## Migration status

The former TypeScript protocol scaffold was retired by GUI020 after the native CMake workspace and replacement structural tests were installed. Historical commits remain unchanged and recoverable.

## Contribution licence

By submitting a contribution, you certify that you have the right to provide it and agree that you retain your copyright while granting Ahmed Elkholy a perpetual, worldwide, irrevocable, nonexclusive, royalty-free right to use, reproduce, modify, distribute, sublicense, and offer the contribution under this repository's PolyForm Noncommercial terms and under separately negotiated commercial agreements. Clearly identified third-party material is accepted only with compatible written redistribution and commercial-use rights; submitting it does not override its original licence.
