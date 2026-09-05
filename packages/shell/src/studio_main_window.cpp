// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/core/application_info.hpp"
#include "aimora/studio/protocol/generated/service_protocol.hpp"
#include "aimora/studio/protocol/service_client.hpp"
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/shell/command_line_history.hpp"

#include <QKeyEvent>
#include "aimora/studio/shell/drawing_scene_transport.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QFont>
#include <QJsonArray>
#include <QInputDialog>
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

StudioMainWindow::StudioMainWindow(themes::ThemeController& themeController,
                                   QSettings& settings,
                                   QWidget* parent)
    : QMainWindow{parent}, themeController_{themeController}, workspaceSettings_{settings} {
    configureWindow();
    createMenus();
    createPanels();
    static_cast<void>(drawingWorkspace_->setCustomCommandAliases(workspaceSettings_.customCommandAliases()));
    refreshDrawingAliasLabels();
    drawingWorkspace_->setCommandInputHandler([this](const QString& text) {
        if (StudioDockWidget* commandDock = panel(QStringView{u"panel.command-line"}); commandDock != nullptr) {
            commandDock->show();
            commandDock->raise();
        }
        commandLine_->setText(text);
        commandLine_->setFocus(Qt::ShortcutFocusReason);
    });
    QAction* cancelAction = registerAction(QStringLiteral("edit.cancel"), tr("Cancel Command"),
                                           QStringLiteral("Edit"), QKeySequence{Qt::Key_Escape});
    connect(cancelAction, &QAction::triggered, this, [this]() {
        static_cast<void>(drawingWorkspace_->executeCommandText(QStringView{u"cancel"}));
        commandLine_->clear();
        drawingWorkspace_->interactionSurface()->setFocus(Qt::ShortcutFocusReason);
    });
    connect(catalogLibrary_,
            &catalog::CatalogLibraryWidget::placementRequested,
            this,
            [this](const QString& catalogId, const bool assembly) {
                static_cast<void>(drawingWorkspace_->requestEquipmentPlacement(
                    catalogId, assembly, drawingWorkspace_->precisionViewport().center));
            });
    drawingWorkspace_->setInspectionSelectionHandler(
        [this](const QVector<canvas::SceneItemId>& selectedIds, bool quickEdit) {
            if (selectedIds.isEmpty()) {
                schemaInspector_->clearInspection();
                return;
            }
            if (!inspectionIdentityResolver_) {
                return;
            }
            const auto identity = inspectionIdentityResolver_(selectedIds);
            if (!identity.has_value()) {
                schemaInspector_->clearInspection();
                return;
            }
            schemaInspector_->inspect(*identity);
            if (StudioDockWidget* inspectorDock = panel(QStringView{u"panel.inspector"});
                inspectorDock != nullptr) {
                inspectorDock->show();
                inspectorDock->raise();
                if (quickEdit) {
                    schemaInspector_->setFocus(Qt::ShortcutFocusReason);
                }
            }
        });

    connect(&themeController_,
            &themes::ThemeController::themeChanged,
            this,
            [this](themes::ThemeMode, themes::ThemeMode) {
                updateTheme();
                updateThemeActions();
            });
    updateTheme();
    updateThemeActions();

    restoreStatus_ = workspaceSettings_.restore(*this);
    if (restoreStatus_ == WorkspaceRestoreStatus::Restored) {
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

void StudioMainWindow::bindProjectService(protocol::ServiceClient* client) {
    disconnect(projectResponseConnection_);
    disconnect(projectStateConnection_);
    disconnect(projectDestroyedConnection_);
    projectClient_ = client;
    pendingProjectOpen_.clear();
    pendingProjectSave_.clear();
    if (QAction* save = commandAction(QStringView{u"file.save"}); save != nullptr) {
        save->setEnabled(false);
    }
    if (client == nullptr) {
        bindSemanticEditService(nullptr, {}, {});
        return;
    }
    projectStateConnection_ = connect(client, &protocol::ServiceClient::stateChanged, this,
        [this](protocol::ServiceClient::State state) {
            if (state == protocol::ServiceClient::State::Failed ||
                state == protocol::ServiceClient::State::Disconnected ||
                state == protocol::ServiceClient::State::Closing) {
                handleProjectServiceUnavailable();
            }
        });
    projectDestroyedConnection_ = connect(client, &QObject::destroyed, this,
        [this]() { handleProjectServiceUnavailable(); });
    projectResponseConnection_ = connect(
        client, &protocol::ServiceClient::responseReceived, this,
        [this, client](const QString& requestId, bool ok, const QJsonObject& result,
                       const QString&, const QString& errorMessage) {
            if (!pendingProjectSave_.isEmpty() && requestId == pendingProjectSave_) {
                pendingProjectSave_.clear();
                if (ok && result.value(QStringLiteral("saved")).toBool() &&
                    result.value(QStringLiteral("revision")).toString() == semanticRevision_) {
                    setWindowModified(false);
                } else {
                    QMessageBox::warning(this, tr("Save Project"), errorMessage.isEmpty() ?
                        tr("The service did not confirm saving the current revision.") : errorMessage);
                }
                return;
            }
            if (requestId != pendingProjectOpen_ || pendingProjectOpen_.isEmpty()) {
                return;
            }
            pendingProjectOpen_.clear();
            if (!ok) {
                QMessageBox::warning(this, tr("Open Project"), errorMessage);
                return;
            }
            const QString projectId = result.value(QStringLiteral("project_id")).toString();
            const QString revision = result.value(QStringLiteral("revision")).toString();
            if (projectId.isEmpty() || revision.isEmpty() ||
                result.value(QStringLiteral("drawing_view_id")).toString().isEmpty()) {
                QMessageBox::warning(this, tr("Open Project"), tr("The service returned an incomplete drawing session."));
                return;
            }
            if (!applyDrawingScene(result.value(QStringLiteral("drawing_scene")).toObject())) {
                return;
            }
            bindInspectionService(client);
            bindSemanticEditService(client, projectId, revision);
            const QJsonArray operations = result.value(QStringLiteral("edit_operations")).toArray();
            for (const QString& id : {QStringLiteral("edit.undo"), QStringLiteral("edit.redo")}) {
                if (QAction* action = commandAction(QStringView{id}); action != nullptr) {
                    const QString availability = id == QStringLiteral("edit.undo") ?
                        QStringLiteral("can_undo") : QStringLiteral("can_redo");
                    action->setEnabled(operations.contains(id) && result.value(availability).toBool());
                }
            }
            for (const QString& id : {QStringLiteral("draw.line"), QStringLiteral("draw.polyline"),
                                      QStringLiteral("draw.rectangle"),
                                      QStringLiteral("draw.text"),
                                      QStringLiteral("draw.circle"),
                                      QStringLiteral("draw.arc"),
                                      QStringLiteral("draw.ellipse"),
                                      QStringLiteral("modify.mirror_horizontal"),
                                      QStringLiteral("modify.mirror_vertical"),
                                      QStringLiteral("modify.rotate_quarter"),
                                      QStringLiteral("modify.text"),
                                      QStringLiteral("modify.explode_paths"),
                                      QStringLiteral("modify.join_lines"),
                                      QStringLiteral("modify.align_anchor_x"), QStringLiteral("modify.align_anchor_y"),
                                      QStringLiteral("modify.distribute_anchor_x"), QStringLiteral("modify.distribute_anchor_y"),
                                      QStringLiteral("modify.copy"),
                                      QStringLiteral("modify.scale"),
                                      QStringLiteral("modify.move"), QStringLiteral("modify.erase")}) {
                if (QAction* action = commandAction(QStringView{id}); action != nullptr) {
                    action->setEnabled(operations.contains(id));
                }
            }
            setWindowModified(result.value(QStringLiteral("modified")).toBool());
            if (QAction* save = commandAction(QStringView{u"file.save"}); save != nullptr) {
                save->setEnabled(result.value(QStringLiteral("can_save")).toBool() &&
                    client->capabilities().contains(QStringLiteral("project.save")));
            }
            setWindowTitle(tr("%1[*] - %2").arg(result.value(QStringLiteral("display_name")).toString(),
                                            core::ApplicationInfo::productName()));
        });
}

void StudioMainWindow::handleProjectServiceUnavailable() {
    const bool interrupted = !pendingProjectSave_.isEmpty() || !pendingSemanticRequests_.isEmpty();
    if (interrupted) {
        setWindowModified(true);
    }
    bindInspectionService(nullptr);
    bindProjectService(nullptr);
    if (interrupted && commandLine_ != nullptr) {
        const QString explanation = tr("Service connection lost. The pending operation's outcome is unknown; the last displayed scene is retained.");
        commandLine_->setProperty("aimoraInputRejected", true);
        commandLine_->setPlaceholderText(explanation);
        commandLine_->setAccessibleDescription(explanation);
        for (StudioDockWidget* dock : panels()) {
            if (dock->isAncestorOf(commandLine_)) {
                dock->show();
            }
        }
    }
}

bool StudioMainWindow::openDrawingProject(const QString& path) {
    if (isWindowModified()) {
        QMessageBox::warning(this, tr("Unsaved Drawing"), tr("Save the current drawing before opening another project."));
        return false;
    }
    if (projectClient_ == nullptr || !projectClient_->isReady() ||
        !pendingProjectOpen_.isEmpty() || !pendingProjectSave_.isEmpty() ||
        !pendingSemanticRequests_.isEmpty() || path.trimmed().isEmpty()) {
        return false;
    }
    pendingProjectOpen_ = projectClient_->sendRequest(
        protocol::generated::Method::ProjectOpen,
        {{QStringLiteral("path"), path}, {QStringLiteral("mode"), QStringLiteral("drafting")}});
    return !pendingProjectOpen_.isEmpty();
}

bool StudioMainWindow::saveDrawingProject() {
    if (projectClient_ == nullptr || !projectClient_->isReady() || semanticProjectId_.isEmpty() ||
        semanticRevision_.isEmpty() || !pendingProjectSave_.isEmpty() ||
        !pendingProjectOpen_.isEmpty() || !pendingSemanticRequests_.isEmpty() ||
        !projectClient_->capabilities().contains(QStringLiteral("project.save"))) {
        return false;
    }
    pendingProjectSave_ = projectClient_->sendRequest(protocol::generated::Method::ProjectSave,
        {{QStringLiteral("project_id"), semanticProjectId_},
         {QStringLiteral("base_revision"), semanticRevision_}});
    return !pendingProjectSave_.isEmpty();
}

bool StudioMainWindow::createDrawingProject(const QString& path, const QString& name) {
    if (isWindowModified()) {
        QMessageBox::warning(this, tr("Unsaved Drawing"), tr("Save the current drawing before creating another project."));
        return false;
    }
    if (projectClient_ == nullptr || !projectClient_->isReady() ||
        !projectClient_->capabilities().contains(QStringLiteral("project.create")) ||
        !pendingProjectOpen_.isEmpty() || !pendingProjectSave_.isEmpty() ||
        !pendingSemanticRequests_.isEmpty() || path.trimmed().isEmpty() || name.trimmed().isEmpty()) {
        return false;
    }
    pendingProjectOpen_ = projectClient_->sendRequest(protocol::generated::Method::ProjectCreate,
        {{QStringLiteral("path"), path}, {QStringLiteral("name"), name}});
    return !pendingProjectOpen_.isEmpty();
}

void StudioMainWindow::bindSemanticEditService(protocol::ServiceClient* client,
                                               QString projectId,
                                               QString baseRevision) {
    disconnect(semanticResponseConnection_);
    disconnect(semanticClientDestroyedConnection_);
    semanticEditClient_ = client;
    drawingWorkspace_->setCanonicalEditConfirmationRequired(client != nullptr);
    semanticProjectId_ = std::move(projectId);
    semanticRevision_ = std::move(baseRevision);
    pendingSemanticRequests_.clear();
    for (const QString& id : {QStringLiteral("draw.line"), QStringLiteral("draw.polyline"),
                              QStringLiteral("draw.rectangle"),
                              QStringLiteral("draw.text"),
                              QStringLiteral("draw.circle"),
                              QStringLiteral("draw.arc"),
                              QStringLiteral("draw.ellipse"),
                              QStringLiteral("modify.mirror_horizontal"),
                              QStringLiteral("modify.mirror_vertical"),
                              QStringLiteral("modify.rotate_quarter"),
                              QStringLiteral("modify.text"),
                              QStringLiteral("modify.explode_paths"),
                              QStringLiteral("modify.join_lines"),
                              QStringLiteral("modify.align_anchor_x"), QStringLiteral("modify.align_anchor_y"),
                              QStringLiteral("modify.distribute_anchor_x"), QStringLiteral("modify.distribute_anchor_y"),
                              QStringLiteral("edit.undo"), QStringLiteral("edit.redo"),
                              QStringLiteral("modify.copy"),
                              QStringLiteral("modify.scale"),
                              QStringLiteral("modify.move"), QStringLiteral("modify.erase")}) {
        if (QAction* action = commandAction(QStringView{id}); action != nullptr) {
            action->setEnabled(false);
        }
    }
    drawingWorkspace_->setCanonicalEditHandler(
        [this](const commands::CanonicalEditRequest& request) {
            return dispatchSemanticEdit(request);
        });
    if (client == nullptr) {
        return;
    }
    semanticClientDestroyedConnection_ = connect(client, &QObject::destroyed, this, [this]() {
        semanticEditClient_ = nullptr;
        semanticProjectId_.clear();
        semanticRevision_.clear();
        pendingSemanticRequests_.clear();
        drawingWorkspace_->setCanonicalEditHandler({});
    });
    semanticResponseConnection_ = connect(
        client,
        &protocol::ServiceClient::responseReceived,
        this,
        [this](const QString& requestId,
               const bool ok,
               const QJsonObject& result,
               const QString&,
               const QString& errorMessage) {
            if (!pendingSemanticRequests_.remove(requestId)) {
                return;
            }
            const bool accepted = ok &&
                result.value(QStringLiteral("status")).toString() == QStringLiteral("accepted");
            drawingWorkspace_->resolveCanonicalEdit(accepted);
            if (accepted) {
                for (const QString& id : {QStringLiteral("edit.undo"), QStringLiteral("edit.redo")}) {
                    if (QAction* action = commandAction(QStringView{id}); action != nullptr) {
                        action->setEnabled(result.value(id == QStringLiteral("edit.undo") ?
                            QStringLiteral("can_undo") : QStringLiteral("can_redo")).toBool());
                    }
                }
                if (commandLine_ != nullptr) {
                    commandLine_->setProperty("aimoraInputRejected", false);
                    commandLine_->setPlaceholderText(tr("Command or coordinates"));
                }
                const QString revision = result.value(QStringLiteral("revision")).toString();
                if (!revision.isEmpty()) {
                    semanticRevision_ = revision;
                    setWindowModified(result.value(QStringLiteral("modified")).toBool(true));
                }
                if (result.contains(QStringLiteral("drawing_scene"))) {
                    static_cast<void>(applyDrawingScene(result.value(QStringLiteral("drawing_scene")).toObject()));
                }
            } else if (commandLine_ != nullptr) {
                const QString explanation = errorMessage.isEmpty() ?
                    tr("The edit was rejected. The gesture is retained; correct it or press Esc to cancel.") : errorMessage;
                commandLine_->setProperty("aimoraInputRejected", true);
                commandLine_->setPlaceholderText(explanation);
                commandLine_->setAccessibleDescription(explanation);
                for (StudioDockWidget* dock : panels()) {
                    if (dock->isAncestorOf(commandLine_)) {
                        dock->show();
                    }
                }
                commandLine_->setFocus(Qt::OtherFocusReason);
            }
        });
}

QString StudioMainWindow::semanticRevision() const {
    return semanticRevision_;
}

bool StudioMainWindow::applyDrawingScene(const QJsonObject& payload) {
    auto decoded = decodeDrawingScene(payload, themeController_.tokens().textPrimary);
    if (!decoded.scene) {
        QMessageBox::warning(this, tr("Drawing Display"), decoded.error);
        return false;
    }
    drawingWorkspace_->setScene(std::move(decoded.scene));
    drawingWorkspace_->setSemanticItemIds(std::move(decoded.ownerIds));
    if (!decoded.unsupportedOwnerIds.isEmpty()) {
        QMessageBox::warning(this, tr("Drawing Display"),
            tr("%1 drawing objects are not yet supported by this display path.")
                .arg(decoded.unsupportedOwnerIds.size()));
    }
    return true;
}

bool StudioMainWindow::dispatchSemanticEdit(const commands::CanonicalEditRequest& request) {
    if (!pendingProjectOpen_.isEmpty() || !pendingProjectSave_.isEmpty() || !pendingSemanticRequests_.isEmpty() ||
        semanticEditClient_ == nullptr || !semanticEditClient_->isReady() ||
        !semanticEditClient_->capabilities().contains(QStringLiteral("semantic.transaction")) ||
        semanticProjectId_.trimmed().isEmpty() || semanticRevision_.trimmed().isEmpty() ||
        (request.semanticIds.isEmpty() && request.commandId != QStringLiteral("layout.initial") &&
         request.commandId != QStringLiteral("layout.full") &&
         request.commandId != QStringLiteral("draw.line") &&
         request.commandId != QStringLiteral("draw.polyline") &&
         request.commandId != QStringLiteral("draw.rectangle") &&
         request.commandId != QStringLiteral("draw.circle") &&
         request.commandId != QStringLiteral("draw.arc") &&
         request.commandId != QStringLiteral("draw.ellipse") &&
         request.commandId != QStringLiteral("edit.undo") &&
         request.commandId != QStringLiteral("edit.redo") &&
         request.commandId != QStringLiteral("draw.text"))) {
        return false;
    }
    QString operation = request.commandId;
    if (operation == QStringLiteral("electrical.connect")) {
        operation = QStringLiteral("conductor.connect");
    }
    static const QSet<QString> supportedOperations{
        QStringLiteral("equipment.place"),
        QStringLiteral("conductor.connect"),
        QStringLiteral("junction.update"),
        QStringLiteral("projection.edit"),
        QStringLiteral("route.edit"),
        QStringLiteral("projection.remove"),
        QStringLiteral("asset.delete"),
        QStringLiteral("cross_reference.update"),
        QStringLiteral("layout.initial"),
        QStringLiteral("layout.full"),
        QStringLiteral("layout.local"),
        QStringLiteral("layout.incremental"),
        QStringLiteral("draw.line"),
        QStringLiteral("draw.polyline"),
        QStringLiteral("draw.rectangle"),
        QStringLiteral("draw.text"),
        QStringLiteral("draw.circle"),
        QStringLiteral("draw.arc"),
        QStringLiteral("draw.ellipse"),
        QStringLiteral("modify.mirror_horizontal"),
        QStringLiteral("modify.mirror_vertical"),
        QStringLiteral("modify.rotate_quarter"),
        QStringLiteral("modify.text"),
        QStringLiteral("modify.explode_paths"),
        QStringLiteral("modify.join_lines"),
        QStringLiteral("modify.align_anchor_x"), QStringLiteral("modify.align_anchor_y"),
        QStringLiteral("modify.distribute_anchor_x"), QStringLiteral("modify.distribute_anchor_y"),
        QStringLiteral("edit.undo"), QStringLiteral("edit.redo"),
        QStringLiteral("modify.move"),
        QStringLiteral("modify.copy"),
        QStringLiteral("modify.scale"),
        QStringLiteral("modify.erase"),
    };
    if (!supportedOperations.contains(operation)) {
        return false;
    }
    QJsonArray semanticIds;
    for (const QString& semanticId : request.semanticIds) {
        semanticIds.append(semanticId);
    }
    QJsonArray points;
    for (const QPointF& point : request.points) {
        points.append(QJsonArray{point.x(), point.y()});
    }
    QJsonObject attributes = request.attributes;
    if (operation == QStringLiteral("modify.scale") && !attributes.contains(QStringLiteral("factor"))) {
        bool accepted = false;
        const QString factor = QInputDialog::getText(this, tr("Scale Drafting Geometry"),
            tr("Positive scale factor (geometry only; text and blocks are not supported):"),
            QLineEdit::Normal, QStringLiteral("1"), &accepted);
        if (!accepted || factor.trimmed().isEmpty()) {
            return false;
        }
        attributes.insert(QStringLiteral("factor"), factor.trimmed());
    }
    if ((operation == QStringLiteral("draw.text") || operation == QStringLiteral("modify.text")) &&
        !attributes.contains(QStringLiteral("text"))) {
        const bool replacement = operation == QStringLiteral("modify.text");
        if (replacement && semanticIds.isEmpty()) {
            return false;
        }
        bool accepted = false;
        const QString text = QInputDialog::getText(this,
            replacement ? tr("Edit Drafting Text") : tr("Text Annotation"),
            replacement ? tr("Replacement text for all selected drafting labels:") : tr("Text:"),
                                                  QLineEdit::Normal, {}, &accepted);
        if (!accepted || text.trimmed().isEmpty()) {
            return false;
        }
        attributes.insert(QStringLiteral("text"), text);
    }
    QJsonArray presentationItemIds;
    for (const quint64 itemId : request.selectedItemIds) {
        presentationItemIds.append(QString::number(itemId));
    }
    if (!presentationItemIds.isEmpty()) {
        attributes.insert(QStringLiteral("presentation_item_ids"), presentationItemIds);
    }
    const QJsonObject parameters{
        {QStringLiteral("project_id"), semanticProjectId_},
        {QStringLiteral("base_revision"), semanticRevision_},
        {QStringLiteral("transaction_id"), QStringLiteral("studio-%1").arg(request.serial)},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("semantic_ids"), semanticIds},
        {QStringLiteral("points"), points},
        {QStringLiteral("attributes"), attributes},
    };
    const QString requestId =
        semanticEditClient_->sendRequest(protocol::generated::Method::SemanticCommit, parameters);
    if (requestId.isEmpty()) {
        return false;
    }
    pendingSemanticRequests_.insert(requestId);
    return true;
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
    std::sort(result.begin(),
              result.end(),
              [](const StudioDockWidget* first, const StudioDockWidget* second) {
                  return first->panelId() < second->panelId();
              });
    return result;
}

QStringList StudioMainWindow::menuTitles() const {
    QStringList titles;
    const QList<QAction*> menuActions = menuBar()->actions();
    titles.reserve(menuActions.size());
    for (const QAction* menuAction : menuActions) {
        titles.push_back(normalizedMenuTitle(menuAction->text()));
    }
    return titles;
}

WorkspaceRestoreStatus StudioMainWindow::restoreStatus() const noexcept {
    return restoreStatus_;
}

bool StudioMainWindow::shouldStartMaximized() const {
    return restoreStatus_ == WorkspaceRestoreStatus::Restored ? workspaceSettings_.wasMaximized()
                                                              : true;
}

void StudioMainWindow::saveWorkspace() {
    workspaceSettings_.save(*this);
    for (const StudioDockWidget* dock : panels()) {
        workspaceSettings_.savePanelPinned(dock->panelId(), dock->isPinned());
    }
}

void StudioMainWindow::resetWorkspace() {
    workspaceSettings_.clearLayout();
    applyDefaultWorkspace();
    restoreStatus_ = WorkspaceRestoreStatus::NoSavedState;
}

void StudioMainWindow::closeEvent(QCloseEvent* event) {
    if (!pendingProjectSave_.isEmpty() || !pendingSemanticRequests_.isEmpty()) {
        event->ignore();
        return;
    }
    if (isWindowModified() && QMessageBox::warning(this, tr("Unsaved Drawing"),
            tr("The drawing has unsaved changes. Cancel to save it, or discard the changes."),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) {
        event->ignore();
        return;
    }
    saveWorkspace();
    QMainWindow::closeEvent(event);
}

void StudioMainWindow::configureWindow() {
    setObjectName(QStringLiteral("aimora.main-window"));
    setWindowTitle(tr("%1 — Drawing Workspace").arg(core::ApplicationInfo::productName()));
    setMinimumSize(minimumWindowWidth, minimumWindowHeight);
    resize(defaultWindowWidth, defaultWindowHeight);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks | QMainWindow::GroupedDragging);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    menuBar()->setNativeMenuBar(true);

    drawingWorkspace_ = new DrawingWorkspace{this};
    setCentralWidget(drawingWorkspace_);
}

void StudioMainWindow::applyDefaultWorkspace() {
    for (StudioDockWidget* dock : panels()) {
        dock->setPinned(false);
        dock->setFloating(false);
        addDockWidget(defaultDockAreas_.value(dock->panelId()), dock);
        dock->hide();
    }
    resize(defaultWindowWidth, defaultWindowHeight);
}

void StudioMainWindow::restorePanelPins() {
    for (StudioDockWidget* dock : panels()) {
        dock->setPinned(workspaceSettings_.panelPinned(dock->panelId()));
    }
}

void StudioMainWindow::updateTheme() {
    drawingWorkspace_->setThemeTokens(themeController_.tokens());
    update();
}

void StudioMainWindow::updateThemeActions() {
    const QString requested = themes::toString(themeController_.requestedMode());
    for (QAction* action : themeActionGroup_->actions()) {
        const QSignalBlocker blocker{action};
        action->setChecked(action->data().toString() == requested);
    }
}

void StudioMainWindow::showAboutDialog() {
    QMessageBox dialog{this};
    dialog.setWindowTitle(tr("About AIMORAStudio"));
    dialog.setIcon(QMessageBox::Information);
    dialog.setText(
        tr("<b>%1 %2</b>")
            .arg(core::ApplicationInfo::productName(), core::ApplicationInfo::version()));
    dialog.setInformativeText(
        tr("Native C++20 and Qt 6 desktop shell with an out-of-process Julia "
           "engineering service. GUI030 provides the clean shell and themes; "
           "engineering behavior remains in later dependency-ordered packets."));
    dialog.setStandardButtons(QMessageBox::Close);
    dialog.exec();
}

QMenu* StudioMainWindow::menu(QStringView menuId) const {
    return menus_.value(menuId.toString(), nullptr);
}

QAction* StudioMainWindow::registerAction(QString id,
                                          const QString& label,
                                          const QString& category,
                                          const QKeySequence& shortcut) {
    const commands::CommandDefinition definition{
        .id = id,
        .label = label,
        .category = category,
        .defaultShortcut = shortcut,
    };
    const commands::RegistrationResult result = commandRegistry_.registerCommand(definition);
    if (result != commands::RegistrationResult::Added) {
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

StudioDockWidget* StudioMainWindow::addPanel(QString panelId,
                                             const QString& title,
                                             Qt::DockWidgetArea defaultArea,
                                             QWidget* content) {
    StudioDockWidget* dock = new StudioDockWidget{panelId, title, content, this};
    defaultDockAreas_.insert(panelId, defaultArea);
    panels_.insert(std::move(panelId), dock);
    addDockWidget(defaultArea, dock);
    dock->hide();
    return dock;
}

QWidget* StudioMainWindow::createInformationPanel(const QString& title,
                                                  const QString& description) const {
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

namespace {

class DrawingCommandLineEdit final : public QLineEdit {
  public:
    explicit DrawingCommandLineEdit(QWidget* parent) : QLineEdit{parent} {}

  protected:
    void keyPressEvent(QKeyEvent* event) override {
        const auto modifiers = event->modifiers();
        if (event->key() == Qt::Key_Space &&
            !modifiers.testFlag(Qt::ControlModifier) &&
            !modifiers.testFlag(Qt::AltModifier) &&
            !modifiers.testFlag(Qt::MetaModifier)) {
            event->accept();
            if (!event->isAutoRepeat()) {
                Q_EMIT returnPressed();
            }
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

} // namespace

QWidget* StudioMainWindow::createCommandPanel() {
    QWidget* panelContent = new QWidget;
    panelContent->setProperty("aimoraPanel", true);
    panelContent->setAccessibleName(tr("Command line"));

    auto* layout = new QVBoxLayout{panelContent};
    layout->setContentsMargins(12, 12, 12, 12);

    commandLine_ = new DrawingCommandLineEdit{panelContent};
    auto* commandHistory = new CommandLineHistory{*commandLine_};
    auto* historyAction = new QAction{tr("Command input history"), this};
    historyAction->setObjectName(QStringLiteral("aimora.command-history"));
    historyAction->setShortcut(QKeySequence{Qt::Key_F2});
    addAction(historyAction);
    connect(historyAction, &QAction::triggered, commandHistory, [commandHistory]() {
        commandHistory->toggleHistory();
    });
    commandLine_->setObjectName(QStringLiteral("aimora.command-line"));
    commandLine_->setPlaceholderText(
        tr("Command or coordinates: LINE, PL, MOVE, GRID, SNAP, ORTHO, POLAR"));
    commandLine_->setAccessibleDescription(
        tr("Enter a command alias, absolute coordinates, relative @x,y coordinates, "
           "or polar @distance<angle coordinates. Enter or Space submits input; "
           "empty input completes or repeats a command. Escape cancels."));
    commandLine_->setToolTip(
        tr("Enter / Space: submit, complete, or repeat\n"
           "Esc: cancel\n"
           "Coordinates: 100,50 | @100,50 | @100<45\n"
           "Active polyline: U undoes a vertex; C closes and submits\n"
           "Circle: enter center coordinates, then a positive radius to submit\n"
           "Scale: select geometry, SC, pivot coordinates, then a positive factor\n"
           "Ctrl+A on canvas: select drawing objects; in this field: select text\n"
           "Zoom: Z then E fits drawing extents; ZE also fits directly\n"
           "F3: object snap | F7: grid display | F9: grid snap | F8: ortho | F10: polar"));
    connect(commandLine_, &QLineEdit::returnPressed, this, [this]() {
        if (drawingWorkspace_->executeCommandText(commandLine_->text())) {
            commandLine_->clear();
            commandLine_->setProperty("aimoraInputRejected", false);
            for (const QString& actionId : {QStringLiteral("view.grid-snap"),
                                            QStringLiteral("view.grid-display"),
                                            QStringLiteral("view.object-snap"),
                                            QStringLiteral("view.ortho"),
                                            QStringLiteral("view.polar")}) {
                if (QAction* action = commandAction(QStringView{actionId}); action != nullptr) {
                    const QSignalBlocker blocker{action};
                    if (actionId.endsWith(QStringLiteral("grid-snap"))) {
                        action->setChecked(drawingWorkspace_->gridSnapEnabled());
                    } else if (actionId.endsWith(QStringLiteral("grid-display"))) {
                        action->setChecked(drawingWorkspace_->gridVisible());
                    } else if (actionId.endsWith(QStringLiteral("object-snap"))) {
                        action->setChecked(drawingWorkspace_->objectSnapEnabled());
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
