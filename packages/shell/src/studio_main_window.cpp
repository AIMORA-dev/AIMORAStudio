// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include "aimora/studio/core/application_info.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace aimora::studio::shell {
namespace {

constexpr int defaultWindowWidth = 1440;
constexpr int defaultWindowHeight = 900;
constexpr int minimumWindowWidth = 800;
constexpr int minimumWindowHeight = 600;

[[nodiscard]] QString normalizedMenuTitle(QString title) {
    title.remove(QLatin1Char('&'));
    return title;
}

} // namespace


StudioMainWindow::StudioMainWindow(
    themes::ThemeController& themeController,
    QSettings& settings,
    QWidget* parent
)
    : QMainWindow{parent},
      themeController_{themeController},
      workspaceSettings_{settings} {
    configureWindow();
    createMenus();
    createPanels();
    drawingWorkspace_->setInspectionSelectionHandler(
        [this](const QVector<canvas::SceneItemId>& selectedIds, bool quickEdit) {
            if(selectedIds.isEmpty()) {
                schemaInspector_->clearInspection();
                return;
            }
            if(!inspectionIdentityResolver_) {
                return;
            }
            const auto identity = inspectionIdentityResolver_(selectedIds);
            if(!identity.has_value()) {
                schemaInspector_->clearInspection();
                return;
            }
            schemaInspector_->inspect(*identity);
            if(StudioDockWidget* inspectorDock = panel(QStringView{u"panel.inspector"});
               inspectorDock != nullptr) {
                inspectorDock->show();
                inspectorDock->raise();
                if(quickEdit) {
                    schemaInspector_->setFocus(Qt::ShortcutFocusReason);
                }
            }
        }
    );

    connect(
        &themeController_,
        &themes::ThemeController::themeChanged,
        this,
        [this](themes::ThemeMode, themes::ThemeMode) {
            updateTheme();
            updateThemeActions();
        }
    );
    updateTheme();
    updateThemeActions();

    restoreStatus_ = workspaceSettings_.restore(*this);
    if(restoreStatus_ == WorkspaceRestoreStatus::Restored) {
        restorePanelPins();
    } else {
        applyDefaultWorkspace();
    }
}

DrawingWorkspace* StudioMainWindow::drawingWorkspace() const noexcept {
    return drawingWorkspace_;
}

catalog::CatalogLibraryWidget* StudioMainWindow::catalogLibrary() const noexcept {
    return catalogLibrary_;
}

inspector::SchemaInspectorWidget* StudioMainWindow::schemaInspector() const noexcept {
    return schemaInspector_;
}

void StudioMainWindow::bindInspectionService(protocol::ServiceClient* client) {
    schemaInspector_->bindServiceClient(client);
}

void StudioMainWindow::setInspectionIdentityResolver(InspectionIdentityResolver resolver) {
    inspectionIdentityResolver_ = std::move(resolver);
}

QAction* StudioMainWindow::commandAction(QStringView commandId) const {
    return actions_.value(commandId.toString(), nullptr);
}

StudioDockWidget* StudioMainWindow::panel(QStringView panelId) const {
    return panels_.value(panelId.toString(), nullptr);
}

QList<StudioDockWidget*> StudioMainWindow::panels() const {
    QList<StudioDockWidget*> result = panels_.values();
    std::sort(
        result.begin(),
        result.end(),
        [](const StudioDockWidget* first, const StudioDockWidget* second) {
            return first->panelId() < second->panelId();
        }
    );
    return result;
}

QStringList StudioMainWindow::menuTitles() const {
    QStringList titles;
    const QList<QAction*> menuActions = menuBar()->actions();
    titles.reserve(menuActions.size());
    for(const QAction* menuAction : menuActions) {
        titles.push_back(normalizedMenuTitle(menuAction->text()));
    }
    return titles;
}

WorkspaceRestoreStatus StudioMainWindow::restoreStatus() const noexcept {
    return restoreStatus_;
}

bool StudioMainWindow::shouldStartMaximized() const {
    return restoreStatus_ == WorkspaceRestoreStatus::Restored
        ? workspaceSettings_.wasMaximized()
        : true;
}

void StudioMainWindow::saveWorkspace() {
    workspaceSettings_.save(*this);
    for(const StudioDockWidget* dock : panels()) {
        workspaceSettings_.savePanelPinned(dock->panelId(), dock->isPinned());
    }
}

void StudioMainWindow::resetWorkspace() {
    workspaceSettings_.clearLayout();
    applyDefaultWorkspace();
    restoreStatus_ = WorkspaceRestoreStatus::NoSavedState;
}

void StudioMainWindow::closeEvent(QCloseEvent* event) {
    saveWorkspace();
    QMainWindow::closeEvent(event);
}

void StudioMainWindow::configureWindow() {
    setObjectName(QStringLiteral("aimora.main-window"));
    setWindowTitle(
        tr("%1 — Drawing Workspace").arg(core::ApplicationInfo::productName())
    );
    setMinimumSize(minimumWindowWidth, minimumWindowHeight);
    resize(defaultWindowWidth, defaultWindowHeight);
    setDockNestingEnabled(true);
    setDockOptions(
        QMainWindow::AllowNestedDocks
        | QMainWindow::AllowTabbedDocks
        | QMainWindow::AnimatedDocks
        | QMainWindow::GroupedDragging
    );
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    menuBar()->setNativeMenuBar(true);

    drawingWorkspace_ = new DrawingWorkspace{this};
    setCentralWidget(drawingWorkspace_);
}

void StudioMainWindow::applyDefaultWorkspace() {
    for(StudioDockWidget* dock : panels()) {
        dock->setPinned(false);
        dock->setFloating(false);
        addDockWidget(defaultDockAreas_.value(dock->panelId()), dock);
        dock->hide();
    }
    resize(defaultWindowWidth, defaultWindowHeight);
}

void StudioMainWindow::restorePanelPins() {
    for(StudioDockWidget* dock : panels()) {
        dock->setPinned(workspaceSettings_.panelPinned(dock->panelId()));
    }
}

void StudioMainWindow::updateTheme() {
    drawingWorkspace_->setThemeTokens(themeController_.tokens());
    update();
}

void StudioMainWindow::updateThemeActions() {
    const QString requested = themes::toString(themeController_.requestedMode());
    for(QAction* action : themeActionGroup_->actions()) {
        const QSignalBlocker blocker{action};
        action->setChecked(action->data().toString() == requested);
    }
}

void StudioMainWindow::showAboutDialog() {
    QMessageBox dialog{this};
    dialog.setWindowTitle(tr("About AIMORAStudio"));
    dialog.setIcon(QMessageBox::Information);
    dialog.setText(
        tr("<b>%1 %2</b>").arg(
            core::ApplicationInfo::productName(),
            core::ApplicationInfo::version()
        )
    );
    dialog.setInformativeText(
        tr(
            "Native C++20 and Qt 6 desktop shell with an out-of-process Julia "
            "engineering service. GUI030 provides the clean shell and themes; "
            "engineering behavior remains in later dependency-ordered packets."
        )
    );
    dialog.setStandardButtons(QMessageBox::Close);
    dialog.exec();
}

QMenu* StudioMainWindow::menu(QStringView menuId) const {
    return menus_.value(menuId.toString(), nullptr);
}

QAction* StudioMainWindow::registerAction(
    QString id,
    const QString& label,
    const QString& category,
    const QKeySequence& shortcut
) {
    const commands::CommandDefinition definition{
        .id = id,
        .label = label,
        .category = category,
        .defaultShortcut = shortcut,
    };
    const commands::RegistrationResult result =
        commandRegistry_.registerCommand(definition);
    if(result != commands::RegistrationResult::Added) {
        qFatal("Invalid or duplicate AIMORAStudio command registration.");
    }

    QAction* action = new QAction{label, this};
    action->setObjectName(id);
    action->setShortcut(shortcut);
    action->setShortcutContext(Qt::WindowShortcut);
    action->setProperty("aimoraCommandCategory", category);
    addAction(action);
    actions_.insert(std::move(id), action);
    return action;
}

StudioDockWidget* StudioMainWindow::addPanel(
    QString panelId,
    const QString& title,
    Qt::DockWidgetArea defaultArea,
    QWidget* content
) {
    StudioDockWidget* dock = new StudioDockWidget{panelId, title, content, this};
    defaultDockAreas_.insert(panelId, defaultArea);
    panels_.insert(std::move(panelId), dock);
    addDockWidget(defaultArea, dock);
    dock->hide();
    return dock;
}

QWidget* StudioMainWindow::createInformationPanel(
    const QString& title,
    const QString& description
) const {
    QWidget* panelContent = new QWidget;
    panelContent->setProperty("aimoraPanel", true);
    panelContent->setAccessibleName(title);

    auto* layout = new QVBoxLayout{panelContent};
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* heading = new QLabel{title, panelContent};
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto* explanation = new QLabel{description, panelContent};
    explanation->setWordWrap(true);
    explanation->setTextInteractionFlags(Qt::TextSelectableByKeyboard);
    layout->addWidget(explanation);
    layout->addStretch(1);
    return panelContent;
}

QWidget* StudioMainWindow::createCommandPanel() {
    QWidget* panelContent = new QWidget;
    panelContent->setProperty("aimoraPanel", true);
    panelContent->setAccessibleName(tr("Command line"));

    auto* layout = new QVBoxLayout{panelContent};
    layout->setContentsMargins(12, 12, 12, 12);

    commandLine_ = new QLineEdit{panelContent};
    commandLine_->setObjectName(QStringLiteral("aimora.command-line"));
    commandLine_->setPlaceholderText(
        tr("Command or coordinates: LINE, PL, MOVE, GRID, ORTHO, POLAR")
    );
    commandLine_->setAccessibleDescription(
        tr("Enter a command alias, absolute coordinates, relative @x,y coordinates, "
           "or polar @distance<angle coordinates.")
    );
    connect(commandLine_, &QLineEdit::returnPressed, this, [this]() {
        if (drawingWorkspace_->executeCommandText(commandLine_->text())) {
            commandLine_->clear();
            commandLine_->setProperty("aimoraInputRejected", false);
            for (const QString& actionId : {QStringLiteral("view.grid-snap"),
                                            QStringLiteral("view.ortho"),
                                            QStringLiteral("view.polar")}) {
                if (QAction* action = commandAction(QStringView{actionId}); action != nullptr) {
                    const QSignalBlocker blocker{action};
                    if (actionId.endsWith(QStringLiteral("grid-snap"))) {
                        action->setChecked(drawingWorkspace_->gridSnapEnabled());
                    } else if (actionId.endsWith(QStringLiteral("ortho"))) {
                        action->setChecked(drawingWorkspace_->orthoEnabled());
                    } else {
                        action->setChecked(drawingWorkspace_->polarEnabled());
                    }
                }
            }
        } else {
            commandLine_->setProperty("aimoraInputRejected", true);
            commandLine_->selectAll();
        }
    });
    layout->addWidget(commandLine_);
    return panelContent;
}

void StudioMainWindow::addUnavailableAction(QMenu& target, const QString& explanation) {
    QAction* unavailable = target.addAction(tr("No commands available in this release"));
    unavailable->setEnabled(false);
    unavailable->setStatusTip(explanation);
    unavailable->setToolTip(unavailable->statusTip());
}

} // namespace aimora::studio::shell
