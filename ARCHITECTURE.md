# AIMORAStudio Native Desktop Architecture

## Status

GUI010 froze the normative native architecture. GUI020 installed the C++20/CMake/Qt repository foundation. GUI030 now implements the first visible drawing-first `QMainWindow`, complete light/dark/system appearance control, optional native dock panels, local workspace persistence, recovery, and shell automation.

This status does not claim Julia connectivity, semantic SLD rendering, equipment editing, CAD operations, engineering publication, DXF exchange, or accepted resource budgets.

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

## Implemented native foundation

The repository contains independent targets for:

- native application entrypoint;
- core application identity and version data;
- bounded local-protocol client configuration;
- renderer-neutral viewport state;
- inspector panel state;
- deterministic command registration;
- semantic theme system;
- drawing-first native shell.

The retired TypeScript/Node scaffold remains available through Git history but is absent from the accepted source tree. The source contract fails if TypeScript, QML, prohibited browser dependencies, or the retired Node entrypoints return to the primary desktop product.

## Implemented GUI030 shell

The shell provides:

```text
QApplication
└── StudioMainWindow
    ├── native menubar
    ├── DrawingWorkspace central widget
    ├── hidden Project Browser dock
    ├── hidden Inspector dock
    ├── hidden Command Line dock
    ├── theme actions
    ├── workspace reset and recovery
    └── native About dialog
```

The permanent menu families are:

```text
File  Edit  View  Draw  Modify  Electrical
Studies  Results  Output  Tools  Help
```

Only the menubar and central drawing surface are visible by default. No ribbon, permanent toolbar, permanent status bar, or always-visible sidebar is created. Unimplemented capability families remain visible but disabled with an exact explanation.

Panels are native `QDockWidget` instances. They can be shown, hidden, docked, floated, and pinned. A pinned panel removes its close feature until it is unpinned. Unique object IDs allow `QMainWindow::saveState()` and `restoreState()` to preserve layout.

Window geometry, dock state, maximized state, and panel pins are stored through `QSettings`. Invalid or incomplete saved state is removed and replaced by the clean default workspace. These settings are local application preferences, not project or physical-model data.

## Theme architecture

`ThemeMode` has exactly three values:

```text
system
light
dark
```

`ThemeController` resolves the requested mode to an effective light or dark scheme, applies the Qt palette and bounded widget stylesheet, listens for system color-scheme changes, persists user choice, and emits one change notification.

`ThemeTokens` owns semantic screen colors for:

- window, panel, alternate panel, and canvas;
- minor and major grid;
- primary, secondary, and disabled text;
- border, accent, accent text, selection, and focus;
- warning, error, success, and conductor.

Light and dark values are committed as versioned JSON fixtures. Tests require opaque valid colors and at least 4.5:1 contrast for normal primary/secondary text and accent text on their declared backgrounds.

The central drawing surface consumes semantic tokens directly and renders a deterministic placeholder grid through `QPainter`. It is a shell qualification surface, not the retained engineering renderer. GUI080 replaces this placeholder with the renderer-neutral scene compiler and accelerated backend.

Screen themes never own paper background, engineering lineweights, plot style, vector PDF, or DXF appearance.

## Supported-platform policy

One source tree and one service protocol target Windows, macOS, and Linux.

Initial Tier-1 qualification targets are:

- Windows 11 x86-64 with a supported MSVC toolchain;
- macOS on Apple Silicon with a supported Apple Clang/Xcode toolchain;
- Ubuntu 24.04 x86-64 with a supported GCC or Clang toolchain.

Windows Arm64, Linux Arm64, and macOS Intel are additional qualification targets and cannot be advertised as supported until their installer, GPU, printing, font, Julia-service, and performance evidence passes. Every release publishes an exact OS/compiler/architecture matrix instead of claiming unrestricted platform support.

Qt's high-DPI scale-factor rounding policy is set before `QApplication` creation. Geometry and token sizes use device-independent coordinates. Fractional scaling, monitor changes, fonts, focus, and accessibility remain release-qualified behavior.

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
- canonical view, drawing, sheet, symbol, workflow, result, visual, and report semantics;
- full-resolution numerical results and scientific caches.

C++ owns only:

- native application and window state;
- active viewport, selection, hover, and local command state;
- temporary interaction previews and uncommitted form edits;
- compiled visible-scene caches and GPU resources;
- selected schema/property slices and bounded result display windows.

C++ must never contain a second physical equipment model or infer engineering meaning from an untyped numeric array.

## Renderer boundary

The interactive canvas uses a custom retained scene and not one `QWidget`, `QObject`, `QGraphicsItem`, or other heavyweight framework object per drawing entity.

The accelerated backend is isolated in one renderer package using `QRhiWidget` and version-matched Qt RHI interfaces. No RHI or Qt private type crosses that package boundary. A deterministic `QPainter` backend provides software fallback, print-preview support, diagnostics, reference rendering, and renderer cross-checks.

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

The same primitive grammar compiles to native interactive scene geometry, deterministic vector PDF commands, and DXF block definitions and inserts.

A CAD line and an electrical connection are different commands and different canonical entity types. Geometrical crossing, imported geometry, symbol movement, or a DXF edit never silently creates, removes, or reconnects physical topology.

## Inspector contract

Clicking a semantic projection resolves its stable asset ID. The client requests a versioned inspector schema and current values from Julia, then creates native controls for the fields returned by the owner.

Supported sections include general data, connections, ratings, equipment construction, study facets, curves and tables, controls and protection, scenarios and events, results, drawing properties, validation, provenance, and revision history.

A completed edit is one typed Julia transaction. Julia validates, commits or rejects it, reports impacts and invalidated results, and returns only affected project/view patches. Unsupported or unavailable study facets are explicit and never simulated by client-side placeholders.

## CAD, sheets, PDF, and DXF

The native editor supports semantic SLD entities and drafting-only entities in the same workspace while preserving their domains.

Canonical sheets support model space, paper space, standard and custom paper sizes, frames, coordinate zones, viewports, scale, layers, title blocks, legends, general notes, revision/approval tables, and generated equipment/cable schedules.

Interactive screen rendering is not publication. Issued PDF output is deterministic vector output generated from canonical drawing and symbol records, with exact paper dimensions, lineweights, searchable text, font policy, plot styles, and multi-page support. A screen capture or raster-only PDF is not acceptable engineering output.

DXF is the first editable AutoCAD exchange format. Export preserves layers, linetypes, lineweights, blocks, inserts, attributes, model/paper layouts, viewports, title blocks, and AIMORA identity metadata. Import is drafting-only by default and classifies every source item as mapped, preserved as drafting, ignored with reason, unsupported, or rejected. Any semantic change requires explicit reviewed mapping and a Julia transaction. Direct DWG support is outside version 1 unless a separately licensed adapter is accepted.

## Memory, quality, security, and accessibility

The release gate measures GUI/service/worker memory, startup, input latency, frame time, publication throughput, result-window memory, cleanup, and long-session stability. The product has no unbounded undo, scene, text, result, diagnostic, thumbnail, or worker cache.

Production C++ uses RAII, value types, explicit ownership, no owning raw pointers, bounded views, checked conversion, deterministic destruction, and no exceptions across protocol or ABI boundaries.

The repository uses strict warnings, formatting, static analysis, sanitizers, unit tests, shell tests, source contracts, committed theme fixtures, and native cross-platform CI. Protocol, drawing, symbol, and DXF parsers receive malformed-input and fuzz testing when introduced.

Menus, dialogs, panels, command entry, and selected canvas objects expose native accessibility semantics. The core workflow is keyboard-operable. Focus, contrast, high-DPI, reduced motion, and color-independent diagnostics are release requirements.

## Qt licence policy

A distributed artifact uses one declared Qt licence route.

An open-source dependency route may use only compatible LGPL modules, dynamically links Qt, provides required notices and licence copies, supplies applicable Qt source/relinking information, records third-party licences, and excludes GPL-only modules. A commercial Qt route requires separately recorded commercial rights and build provenance.

Every release emits an SBOM and exact Qt module inventory. Legal review remains explicit; this architecture document is not legal advice.
