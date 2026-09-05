// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPair>
#include <QSignalBlocker>

namespace aimora::studio::shell {


void StudioMainWindow::createMenus() {
    const QList<QPair<QString, QString>> menuDefinitions{
        {QStringLiteral("file"), tr("&File")},
        {QStringLiteral("edit"), tr("&Edit")},
        {QStringLiteral("view"), tr("&View")},
        {QStringLiteral("draw"), tr("&Draw")},
        {QStringLiteral("modify"), tr("&Modify")},
        {QStringLiteral("electrical"), tr("&Electrical")},
        {QStringLiteral("studies"), tr("&Studies")},
        {QStringLiteral("results"), tr("&Results")},
        {QStringLiteral("output"), tr("&Output")},
        {QStringLiteral("tools"), tr("&Tools")},
        {QStringLiteral("help"), tr("&Help")},
    };

    for(const auto& [id, title] : menuDefinitions) {
        QMenu* created = menuBar()->addMenu(title);
        created->setObjectName(QStringLiteral("menu.") + id);
        menus_.insert(id, created);
    }

    QAction* newProject = registerAction(
        QStringLiteral("file.new"),
        tr("New Project"),
        QStringLiteral("File"),
        QKeySequence::New
    );
    newProject->setEnabled(false);
    newProject->setStatusTip(
        tr("Canonical project creation is introduced by the project/service packets.")
    );
    menu(QStringView{u"file"})->addAction(newProject);

    QAction* openProject = registerAction(
        QStringLiteral("file.open"),
        tr("Open Project"),
        QStringLiteral("File"),
        QKeySequence::Open
    );
    openProject->setEnabled(false);
    openProject->setStatusTip(
        tr("Project sessions become available after the Julia service packet.")
    );
    menu(QStringView{u"file"})->addAction(openProject);

    QAction* saveProject = registerAction(QStringLiteral("file.save"), tr("Save Project"),
        QStringLiteral("File"), QKeySequence::Save);
    saveProject->setEnabled(false);
    saveProject->setStatusTip(tr("Save the current canonical drawing through the Julia service."));
    connect(saveProject, &QAction::triggered, this, [this]() {
        static_cast<void>(saveDrawingProject());
    });
    menu(QStringView{u"file"})->addAction(saveProject);

    menu(QStringView{u"file"})->addSeparator();
    QAction* quitAction = registerAction(
        QStringLiteral("file.quit"),
        tr("Quit"),
        QStringLiteral("File"),
        QKeySequence::Quit
    );
    connect(quitAction, &QAction::triggered, this, []() {
        QCoreApplication::quit();
    });
    menu(QStringView{u"file"})->addAction(quitAction);

    QAction* undoAction = registerAction(
        QStringLiteral("edit.undo"),
        tr("Undo"),
        QStringLiteral("Edit"),
        QKeySequence::Undo
    );
    undoAction->setEnabled(false);
    undoAction->setStatusTip(tr("Undo the last retained canonical drawing transaction."));
    connect(undoAction, &QAction::triggered, drawingWorkspace_, [this]() {
        static_cast<void>(drawingWorkspace_->executeCommandText(QStringView{u"edit.undo"}));
    });
    menu(QStringView{u"edit"})->addAction(undoAction);

    QAction* redoAction = registerAction(
        QStringLiteral("edit.redo"),
        tr("Redo"),
        QStringLiteral("Edit"),
        QKeySequence::Redo
    );
    redoAction->setEnabled(false);
    redoAction->setStatusTip(tr("Redo the last undone canonical drawing transaction."));
    connect(redoAction, &QAction::triggered, drawingWorkspace_, [this]() {
        static_cast<void>(drawingWorkspace_->executeCommandText(QStringView{u"edit.redo"}));
    });
    menu(QStringView{u"edit"})->addAction(redoAction);

    QAction* selectAllAction = registerAction(
        QStringLiteral("edit.select-all"), tr("Select All Drawing Objects (Ctrl+A)"),
        QStringLiteral("Edit"));
    selectAllAction->setStatusTip(tr("Select all displayed drawing objects; Ctrl+A in text fields selects text instead."));
    connect(selectAllAction, &QAction::triggered, drawingWorkspace_, [this]() {
        static_cast<void>(drawingWorkspace_->selectAllDrawingItems());
    });
    menu(QStringView{u"edit"})->addAction(selectAllAction);

    QMenu* themeMenu = menu(QStringView{u"view"})->addMenu(tr("Theme"));
    themeMenu->setObjectName(QStringLiteral("menu.view.theme"));
    themeActionGroup_ = new QActionGroup{this};
    themeActionGroup_->setExclusive(true);

    const QList<QPair<themes::ThemeMode, QString>> themeDefinitions{
        {themes::ThemeMode::System, tr("Follow System")},
        {themes::ThemeMode::Light, tr("Light")},
        {themes::ThemeMode::Dark, tr("Dark")},
    };
    for(const auto& [mode, label] : themeDefinitions) {
        const QString id = QStringLiteral("view.theme.%1").arg(themes::toString(mode));
        QAction* themeAction = registerAction(id, label, QStringLiteral("View"));
        themeAction->setCheckable(true);
        themeAction->setData(themes::toString(mode));
        themeActionGroup_->addAction(themeAction);
        themeMenu->addAction(themeAction);
        connect(themeAction, &QAction::triggered, this, [this, mode]() {
            themeController_.setRequestedMode(mode);
        });
    }

    menu(QStringView{u"view"})->addSeparator();
    QAction* resetWorkspaceAction = registerAction(
        QStringLiteral("view.reset-workspace"),
        tr("Reset Workspace"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("Ctrl+Shift+0")}
    );
    connect(
        resetWorkspaceAction,
        &QAction::triggered,
        this,
        &StudioMainWindow::resetWorkspace
    );
    menu(QStringView{u"view"})->addAction(resetWorkspaceAction);

    QAction* zoomExtentsAction = registerAction(
        QStringLiteral("view.zoom-extents"), tr("Zoom Extents (Z, E)"), QStringLiteral("View"));
    zoomExtentsAction->setStatusTip(tr("Fit drawing geometry and annotations without changing the project."));
    connect(zoomExtentsAction, &QAction::triggered, drawingWorkspace_, [this]() {
        static_cast<void>(drawingWorkspace_->zoomToExtents());
    });
    menu(QStringView{u"view"})->addAction(zoomExtentsAction);

    menu(QStringView{u"view"})->addSeparator();
    QAction* gridSnapAction = registerAction(
        QStringLiteral("view.grid-snap"),
        tr("Grid Snap"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("F9")}
    );
    gridSnapAction->setCheckable(true);
    gridSnapAction->setChecked(drawingWorkspace_->gridSnapEnabled());
    connect(gridSnapAction, &QAction::toggled, drawingWorkspace_, [this](bool enabled) {
        drawingWorkspace_->setGridSnapEnabled(enabled);
    });
    menu(QStringView{u"view"})->addAction(gridSnapAction);

    QAction* gridDisplayAction = registerAction(
        QStringLiteral("view.grid-display"), tr("Display Grid"), QStringLiteral("View"),
        QKeySequence{QStringLiteral("F7")});
    gridDisplayAction->setCheckable(true);
    gridDisplayAction->setAutoRepeat(false);
    gridDisplayAction->setChecked(drawingWorkspace_->gridVisible());
    gridDisplayAction->setStatusTip(tr("Show or hide the drawing grid without changing grid snapping."));
    connect(gridDisplayAction, &QAction::toggled, drawingWorkspace_, [this](bool visible) {
        drawingWorkspace_->setGridVisible(visible);
    });
    menu(QStringView{u"view"})->addAction(gridDisplayAction);

    QAction* objectSnapAction = registerAction(
        QStringLiteral("view.object-snap"),
        tr("Object Snap"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("F3")}
    );
    objectSnapAction->setCheckable(true);
    objectSnapAction->setAutoRepeat(false);
    objectSnapAction->setChecked(drawingWorkspace_->objectSnapEnabled());
    objectSnapAction->setStatusTip(tr("Toggle object snapping without changing engineering connectivity."));
    connect(objectSnapAction, &QAction::toggled, drawingWorkspace_, [this](bool enabled) {
        drawingWorkspace_->setObjectSnapEnabled(enabled);
    });
    menu(QStringView{u"view"})->addAction(objectSnapAction);

    QAction* orthoAction = registerAction(
        QStringLiteral("view.ortho"),
        tr("Ortho Constraint"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("F8")}
    );
    orthoAction->setCheckable(true);
    connect(orthoAction, &QAction::toggled, drawingWorkspace_, [this, orthoAction](bool enabled) {
        drawingWorkspace_->setOrthoEnabled(enabled);
        if (enabled) {
            if (QAction* polar = commandAction(QStringView{u"view.polar"}); polar != nullptr) {
                const QSignalBlocker blocker{polar};
                polar->setChecked(false);
            }
        }
        orthoAction->setChecked(drawingWorkspace_->orthoEnabled());
    });
    menu(QStringView{u"view"})->addAction(orthoAction);

    QAction* polarAction = registerAction(
        QStringLiteral("view.polar"),
        tr("Polar Constraint"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("F10")}
    );
    polarAction->setCheckable(true);
    connect(polarAction, &QAction::toggled, drawingWorkspace_, [this, polarAction](bool enabled) {
        drawingWorkspace_->setPolarEnabled(enabled);
        if (enabled) {
            if (QAction* ortho = commandAction(QStringView{u"view.ortho"}); ortho != nullptr) {
                const QSignalBlocker blocker{ortho};
                ortho->setChecked(false);
            }
        }
        polarAction->setChecked(drawingWorkspace_->polarEnabled());
    });
    menu(QStringView{u"view"})->addAction(polarAction);

    const auto addDrawingAction = [this](const QString& id, const QString& label,
                                         const QString& category, const QString& menuId) {
        QAction* action = registerAction(id, label, category);
        action->setEnabled(false);
        action->setStatusTip(tr("Enter coordinates or pick points; Enter completes and Esc cancels."));
        menu(QStringView{menuId})->addAction(action);
        connect(action, &QAction::triggered, drawingWorkspace_, [this, id]() {
            if (drawingWorkspace_->executeCommandText(QStringView{id})) {
                drawingWorkspace_->interactionSurface()->setFocus(Qt::ShortcutFocusReason);
            }
        });
    };
    addDrawingAction(QStringLiteral("draw.line"), tr("Line (L)"), QStringLiteral("Draw"), QStringLiteral("draw"));
    addDrawingAction(QStringLiteral("draw.polyline"), tr("Polyline (PL)"), QStringLiteral("Draw"), QStringLiteral("draw"));
    commandAction(QStringView{u"draw.polyline"})->setStatusTip(
        tr("Pick vertices or enter coordinates. U undoes a vertex; C closes; Enter finishes; Esc cancels."));
    addDrawingAction(QStringLiteral("draw.rectangle"), tr("Rectangle (REC)"), QStringLiteral("Draw"), QStringLiteral("draw"));
    addDrawingAction(QStringLiteral("draw.text"), tr("Text (T)"), QStringLiteral("Draw"), QStringLiteral("draw"));
    addDrawingAction(QStringLiteral("draw.circle"), tr("Circle (C)"), QStringLiteral("Draw"), QStringLiteral("draw"));
    addDrawingAction(QStringLiteral("draw.arc"), tr("Arc (A)"), QStringLiteral("Draw"), QStringLiteral("draw"));
    addDrawingAction(QStringLiteral("draw.ellipse"), tr("Ellipse (EL)"), QStringLiteral("Draw"), QStringLiteral("draw"));
    commandAction(QStringView{u"draw.ellipse"})->setStatusTip(
        tr("Pick or enter opposite bounding corners for an axis-aligned ellipse; Enter finishes; Esc cancels."));
    commandAction(QStringView{u"draw.arc"})->setStatusTip(
        tr("Pick or enter start, through, and end points. U undoes a point; Enter finishes; Esc cancels."));
    commandAction(QStringView{u"draw.circle"})->setStatusTip(
        tr("Pick or enter the center, then enter a positive radius to finish, or pick a circumference point and press Enter."));
    addDrawingAction(QStringLiteral("modify.move"), tr("Move (M)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    addDrawingAction(QStringLiteral("modify.join_lines"), tr("Join Drafting Lines (J)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    commandAction(QStringView{u"modify.join_lines"})->setStatusTip(
        tr("Select exactly connected drafting lines and press Enter to make one polyline. Gaps, branches, duplicate segments and incompatible styles are rejected; no electrical connectivity is inferred."));
    addDrawingAction(QStringLiteral("modify.explode_paths"), tr("Explode Drafting Paths (X)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    commandAction(QStringView{u"modify.explode_paths"})->setStatusTip(
        tr("Select polylines or rectangles and press Enter to create independent lines. Blocks and electrical objects are not supported; Undo restores the original paths."));
    addDrawingAction(QStringLiteral("modify.text"), tr("Edit Text (ED)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    commandAction(QStringView{u"modify.text"})->setStatusTip(
        tr("Select drafting labels and press Enter to replace their text. Positions and identities stay unchanged; engineering-bound labels cannot be edited here."));
    addDrawingAction(QStringLiteral("modify.align_anchor_x"), tr("Align Anchors X (AX)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    addDrawingAction(QStringLiteral("modify.align_anchor_y"), tr("Align Anchors Y (AY)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    addDrawingAction(QStringLiteral("modify.distribute_anchor_x"), tr("Distribute Anchors X (DX)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    addDrawingAction(QStringLiteral("modify.distribute_anchor_y"), tr("Distribute Anchors Y (DY)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    for (const QString& id : {QStringLiteral("modify.align_anchor_x"), QStringLiteral("modify.align_anchor_y")}) {
        commandAction(QStringView{id})->setStatusTip(tr("Select at least two drafting objects; pick a target coordinate and press Enter. Aligns first defining points or text anchors, not bounding edges."));
    }
    for (const QString& id : {QStringLiteral("modify.distribute_anchor_x"), QStringLiteral("modify.distribute_anchor_y")}) {
        commandAction(QStringView{id})->setStatusTip(tr("Select at least three drafting objects and press Enter. Spaces anchors equally between fixed extremes; non-decimal spacing is rejected without rounding."));
    }
    addDrawingAction(QStringLiteral("modify.rotate_quarter"), tr("Rotate 90 Degrees (R90)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    commandAction(QStringView{u"modify.rotate_quarter"})->setStatusTip(
        tr("Select drafting objects, then pick or enter the rotation pivot. Enter applies a positive 90-degree turn; text stays upright."));
    addDrawingAction(QStringLiteral("modify.mirror_horizontal"), tr("Mirror Horizontal (MIH)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    addDrawingAction(QStringLiteral("modify.mirror_vertical"), tr("Mirror Vertical (MIV)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    commandAction(QStringView{u"modify.mirror_horizontal"})->setStatusTip(
        tr("Select drafting objects, then pick or enter a point on the horizontal mirror line. Enter applies; text stays upright."));
    commandAction(QStringView{u"modify.mirror_vertical"})->setStatusTip(
        tr("Select drafting objects, then pick or enter a point on the vertical mirror line. Enter applies; text stays upright."));
    addDrawingAction(QStringLiteral("modify.copy"), tr("Copy (CO)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    addDrawingAction(QStringLiteral("modify.scale"), tr("Scale Geometry (SC)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    commandAction(QStringView{u"modify.scale"})->setStatusTip(
        tr("Select drafting geometry, pick a pivot, then type a positive scale factor; blank Enter opens the factor dialog."));
    addDrawingAction(QStringLiteral("modify.erase"), tr("Erase (E)"), QStringLiteral("Modify"), QStringLiteral("modify"));
    QAction* equipmentLibraryAction = registerAction(
        QStringLiteral("electrical.equipment-library"),
        tr("Equipment Library"),
        QStringLiteral("Electrical"),
        QKeySequence{QStringLiteral("Ctrl+Shift+L")}
    );
    equipmentLibraryAction->setCheckable(true);
    menu(QStringView{u"electrical"})->addAction(equipmentLibraryAction);
    addUnavailableAction(
        *menu(QStringView{u"studies"}),
        tr("Only accepted studies appear after the Julia service and study UI packets.")
    );
    addUnavailableAction(
        *menu(QStringView{u"results"}),
        tr("Results and overlays are introduced by the study-results packet.")
    );
    addUnavailableAction(
        *menu(QStringView{u"output"}),
        tr("Native printing, vector PDF, and DXF are introduced by publication packets.")
    );
    QAction* aliasesAction = registerAction(QStringLiteral("tools.command-aliases"),
        tr("Command Aliases..."), QStringLiteral("Tools"));
    connect(aliasesAction, &QAction::triggered, this, &StudioMainWindow::showCommandAliases);
    menu(QStringView{u"tools"})->addAction(aliasesAction);

    QAction* aboutAction = registerAction(
        QStringLiteral("help.about"),
        tr("About AIMORAStudio"),
        QStringLiteral("Help"),
        QKeySequence::HelpContents
    );
    connect(aboutAction, &QAction::triggered, this, &StudioMainWindow::showAboutDialog);
    menu(QStringView{u"help"})->addAction(aboutAction);
}

} // namespace aimora::studio::shell
