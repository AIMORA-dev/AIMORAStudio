# AIMORAStudio Native Desktop Architecture

## Status

This document freezes the `GUI010` architecture boundary. It defines the target and ownership rules; it does not claim that the native application is already implemented.

## Product objective

AIMORAStudio shall be a professional native power-system engineering desktop application with:

- ETAP/DIgSILENT-style semantic equipment selection and parameter editing;
- an AutoCAD-like precision drafting experience for single-line diagrams and engineering sheets;
- deterministic vector publication and editable DXF exchange;
- direct integration with the canonical Julia project and accepted AIMORA studies;
- bounded, measurable CPU, GPU, memory, startup, and long-session resource use.

## Language and toolchain boundary

The primary desktop product uses:

```text
C++ standard        C++20
Build system        CMake 3.28+
Qt API family       Qt 6.11
Initial exact pin   Qt 6.11.2
UI technology       Qt 6 Widgets
Engineering         Julia
```

The exact Qt patch, compiler, standard library, CMake version, and deployment runtime are locked in each release manifest. A Qt version change is an explicit dependency migration with native build, renderer, print, accessibility, performance, and licence requalification.

Required runtime Qt modules are limited to `Core`, `Gui`, `Widgets`, `Network`, `PrintSupport`, and `Concurrent`. `Test` is test-only. `ShaderTools` may be build-only when the renderer requires precompiled shaders. Additional modules require a requirement-backed consumer, licence review, and resource measurement.

The first release prohibits Qt Quick, QML, WebEngine, WebView, a bundled browser, JavaScript application code, Qt 3D, and a required Qt SVG module. SVG may exist only as an explicitly admitted derived exchange or report output; it is not the canonical symbol, scene, drawing, or publication intermediate.

## Supported-platform policy

One source tree and one service protocol target Windows, macOS, and Linux.

Initial Tier-1 qualification targets are:

- Windows 11 x86-64 with a supported MSVC toolchain;
- macOS on Apple Silicon with a supported Apple Clang/Xcode toolchain;
- Ubuntu 24.04 x86-64 with a supported GCC or Clang toolchain.

Windows Arm64, Linux Arm64, and macOS Intel are additional qualification targets and cannot be advertised as supported until their installer, GPU, printing, font, Julia-service, and performance evidence passes. Every release publishes an exact OS/compiler/architecture matrix instead of claiming unrestricted platform support.

## Process architecture

```text
AIMORAStudio native process
    native shell, canvas, inspector, interaction state, render caches
            |
            | authenticated framed local protocol
            v
AIMORAService.jl
    canonical project, revisions, commands, schemas, readiness, jobs, artifacts
            |
            | bounded study-worker contract
            v
AIMORA study worker
    accepted study preparation and numerical execution
```

The GUI shall not embed `libjulia` in the first release. `QProcess` owns service and worker lifecycle. `QLocalSocket` or the platform-equivalent local named-pipe endpoint carries the authenticated protocol. Small control messages are schema-generated and human-debuggable; large numerical blocks use bounded binary frames, memory-mapped artifacts, or windowed queries.

The lightweight service starts lazily when an engineering project or service command requires it. Solver-heavy workers start only for accepted requested studies or remain in a bounded reusable pool. Worker termination must return solver memory without closing the GUI or losing the last accepted project revision.

No pointer event generates a service request. No numerical timestep generates a GUI message. Studies return bounded progress, significant events, selected live summaries, event-preserving visualization windows, and immutable artifact references.

## Canonical ownership

Julia owns:

- physical assets, terminals, topology, ratings, parameters, units, provenance, and uncertainty;
- project revisions, transactions, validation, readiness, and result invalidation;
- canonical view, drawing, sheet, symbol, workflow, result, visual, and report semantics in their designated Julia owners;
- full-resolution numerical results and scientific caches.

C++ owns only:

- native application and window state;
- active viewport, selection, hover, and local command state;
- temporary interaction previews and uncommitted form edits;
- compiled visible-scene caches and GPU resources;
- selected schema/property slices and bounded result display windows.

C++ must never contain a second physical equipment model or infer engineering meaning from an untyped numeric array.

## Native shell

The default workspace contains only the native menubar and the central drawing area. A ribbon and permanent sidebar are prohibited by default.

Inspectors, libraries, command input, validation, jobs, results, and report panels appear on demand. They may be overlaid, docked, floated, pinned, or hidden using native `QMainWindow` and `QDockWidget` behavior. Layout persistence is application state, not physical-project semantics.

Dark, light, and system-following themes use semantic tokens. Both themes provide identical commands, states, contrast, focus, diagnostics, plots, and high-DPI behavior. Screen themes never change paper background, engineering lineweights, monochrome plotting, or issued-document appearance.

## Renderer boundary

The interactive canvas uses a custom retained scene and not one `QWidget`, `QObject`, `QGraphicsItem`, or other heavyweight framework object per drawing entity.

The accelerated backend is isolated in one renderer package using public `QRhiWidget` and documented Qt RHI interfaces. The package may be rebuilt or replaced without changing canonical scene semantics. No Qt private header may become a public or canonical dependency. The exact Qt minor is pinned because RHI compatibility is narrower than ordinary Qt source compatibility.

A deterministic `QPainter` backend provides software fallback, print-preview support, diagnostics, reference rendering, and renderer cross-checks.

The scene design requires:

- compact structure-of-arrays or equivalently bounded storage;
- symbol, line, and engineering-text instancing;
- glyph atlases and deterministic text metrics;
- viewport culling and semantic level of detail;
- spatial indexing for selection and snapping;
- dirty-range updates and event-driven redraw;
- explicit CPU/GPU cache limits and disposal;
- no continuous frame loop while the view is idle.

## Symbol and drawing model

A symbol is a projection of a stable Julia-owned engineering asset. The canonical symbol grammar is open text and contains lines, polylines, arcs, circles, ellipses, polygons, semantic ports, snap anchors, label anchors, operating-state variants, level-of-detail variants, styles, provenance, and licence metadata.

The same primitive grammar compiles to:

- native interactive scene geometry;
- deterministic vector PDF drawing commands;
- DXF block definitions and inserts.

A CAD line and an electrical connection are different commands and different canonical entity types. Geometrical crossing, imported geometry, symbol movement, or a DXF edit never silently creates, removes, or reconnects physical topology.

## Inspector contract

Clicking a semantic projection resolves its stable asset ID. The client requests a versioned inspector schema and current values from Julia, then creates native controls for the fields returned by the owner.

Supported sections include general data, connections, ratings, equipment construction, study facets, curves and tables, controls and protection, scenarios and events, results, drawing properties, validation, provenance, and revision history.

A completed edit is one typed Julia transaction. Julia validates, commits or rejects it, reports impacts and invalidated results, and returns only affected project/view patches. Unsupported or unavailable study facets are explicit and never simulated by client-side placeholders.

## CAD, sheets, PDF, and DXF

The native editor supports semantic SLD entities and drafting-only entities in the same workspace while preserving their domains.

Canonical sheets support model space, paper space, standard and custom paper sizes, frames, coordinate zones, viewports, scale, layers, title blocks, legends, general notes, revision/approval tables, and generated equipment/cable schedules.

Interactive screen rendering is not publication. Issued PDF output is deterministic vector output generated from canonical drawing and symbol records, with exact paper dimensions, lineweights, searchable text, font policy, plot styles, and multi-page support. A WebGL/QRhi screenshot or raster-only PDF is not acceptable engineering output.

DXF is the first editable AutoCAD exchange format. Export preserves layers, linetypes, lineweights, blocks, inserts, attributes, model/paper layouts, viewports, title blocks, and AIMORA identity metadata. Import is drafting-only by default and classifies every source item as mapped, ignored with reason, unsupported, or rejected. Any semantic change requires explicit reviewed mapping and a Julia transaction. Direct DWG support is outside version 1 unless a separately licensed adapter is accepted.

## Memory and performance rules

The release gate measures, at minimum:

- GUI-only idle resident memory;
- project-open GUI and service memory;
- study-worker peak memory and post-worker cleanup;
- startup and first-project-open latency;
- pointer-to-frame input latency and frame time;
- layout, PDF, and DXF throughput;
- large-result window memory;
- repeated document open/close and GPU disposal;
- long-session stability.

The product shall have no unbounded undo, scene, text, result, diagnostic, thumbnail, or worker cache. Full project graphs, full waveforms, matrices, and solver states remain in Julia. Performance claims require named hardware and realistic fixtures rather than demo scenes.

## C++ quality and security

Production C++ uses RAII, value types, explicit ownership, no owning raw pointers, bounded views at protocol boundaries, checked integer/length conversion, deterministic destruction, and no exceptions crossing ABI or protocol boundaries.

The repository shall use strict compiler warnings, formatting, static analysis, AddressSanitizer, UndefinedBehaviorSanitizer, and platform-appropriate additional sanitizers in qualified lanes. Protocol, drawing, symbol, and DXF parsers receive malformed-input and fuzz testing.

The local service endpoint uses per-session authentication, version negotiation, input-size limits, path confinement, typed errors, cancellation, and no unrestricted code execution from untrusted payloads. Logs and crash reports contain no private solver paths, credentials, or restricted project data by default.

## Accessibility

Menus, dialogs, inspectors, tables, command entry, and selected canvas objects expose native accessibility semantics. The complete core workflow is keyboard-operable. Focus, contrast, high-DPI, reduced-motion, and color-independent diagnostic meaning are release requirements.

## Qt licence policy

A distributed artifact uses one declared Qt licence route.

An open-source dependency route may use only modules available under a compatible LGPL licence, dynamically links Qt, provides required notices and licence copies, supplies the applicable Qt source/relinking mechanism, records third-party licences, and excludes GPL-only modules. A commercial Qt route requires separately recorded commercial rights and build provenance. The two routes are not silently mixed in one artifact.

Every release emits an SBOM and exact Qt module inventory. Legal review remains explicit; this architecture document is not legal advice.

## Migration rule

The existing TypeScript files are historical scaffolding until `GUI020`. They may be deleted only in the same accepted packet that adds the native CMake workspace, replacement protocol fixtures, native structural tests, and cross-platform CI. No historical commit is rewritten.