# Contributing

AIMORAStudio contributions target the accepted native C++20/Qt 6 desktop architecture. Engineering semantics remain in canonical Julia owners, and generated C++ client types come from the public versioned service schema.

## Required boundaries

- Do not add physical equipment equations, topology rules, units, readiness logic, or study calculations to C++.
- Do not add Electron, Chromium, Node.js runtime, Tauri, Rust, React, TypeScript, browser webviews, Qt Quick, or QML to the primary desktop product.
- Do not create one widget, object, or graphics item per drawing entity.
- Do not infer electrical connectivity from geometry or imported CAD lines.
- Do not make SVG the canonical symbol, scene, drawing, or publication format.
- Do not expose private solver types, paths, source, or evidence through the client protocol.

## C++ and Qt quality

Use C++20, RAII, explicit ownership, value types, deterministic cleanup, checked protocol boundaries, and no owning raw pointers. New code must pass the packet's required formatting, compiler warnings, static analysis, sanitizers, unit tests, interaction tests, resource checks, and native Windows/macOS/Linux CI.

Qt dependencies must remain within the accepted module and licence boundary. A new Qt or third-party dependency requires an executable consumer, resource measurement, licence inventory, and explicit approval.

## Symbols and assets

AIMORA symbols and UI artwork must be original or have exact compatible redistribution and commercial-use rights. Familiar engineering conventions may be implemented independently, but proprietary ETAP, DIgSILENT, AutoCAD, manufacturer, standards-body, logo, font, or drawing assets must not be copied without permission and provenance.

## Migration status

The current TypeScript protocol scaffold is retained as history until `GUI020` installs equivalent native structure and checks. Do not extend the legacy scaffold with new product behavior.

## Contribution licence

By submitting a contribution, you certify that you have the right to provide it and agree that you retain your copyright while granting Ahmed Elkholy a perpetual, worldwide, irrevocable, nonexclusive, royalty-free right to use, reproduce, modify, distribute, sublicense, and offer the contribution under this repository's PolyForm Noncommercial terms and under separately negotiated commercial agreements. Clearly identified third-party material is accepted only with compatible written redistribution and commercial-use rights; submitting it does not override its original licence.