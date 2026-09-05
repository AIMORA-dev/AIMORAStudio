// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/catalog/catalog_library.hpp"
#include "aimora/studio/commands/command_registry.hpp"
#include "aimora/studio/commands/drawing_interaction.hpp"
#include "aimora/studio/inspector/panel_state.hpp"
#include "aimora/studio/inspector/schema_inspector_widget.hpp"
#include "aimora/studio/renderer/scene_surface.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QDockWidget>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QStringView>
#include <QVector>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QAction;
class QActionGroup;
class QCloseEvent;
class QEvent;
class QLineEdit;
class QMenu;

namespace aimora::studio::protocol {
class ServiceClient;
}

namespace aimora::studio::shell {

enum class WorkspaceRestoreStatus : std::uint8_t {
    Restored,
    NoSavedState,
    InvalidState,
};

class DrawingWorkspace final : public QWidget {
  public:
    using CanonicalEditHandler = std::function<bool(const commands::CanonicalEditRequest& request)>;
    using InspectionSelectionHandler =
        std::function<void(const QVector<canvas::SceneItemId>& selectedIds, bool quickEdit)>;

    explicit DrawingWorkspace(QWidget* parent = nullptr);

    void setScene(std::shared_ptr<const canvas::RetainedScene> scene);
    [[nodiscard]] renderer::SceneSurface* sceneSurface() const noexcept;
    void setThemeTokens(const themes::ThemeTokens& tokens);
    [[nodiscard]] const themes::ThemeTokens& themeTokens() const noexcept;
    void setElectricalPortSnaps(QVector<commands::SnapCandidate> ports);
    void setSemanticItemIds(QHash<quint64, QString> semanticItemIds);
    void setCanonicalEditHandler(CanonicalEditHandler handler);
    void setCanonicalEditConfirmationRequired(bool required);
    void resolveCanonicalEdit(bool accepted);
    void setInspectionSelectionHandler(InspectionSelectionHandler handler);
    void setCommandInputHandler(std::function<void(const QString&)> handler);
    [[nodiscard]] bool setCustomCommandAliases(const QHash<QString, QString>& aliases);
    [[nodiscard]] QHash<QString, QString> commandAliases() const;
    [[nodiscard]] bool executeCommandText(QStringView input);
    [[nodiscard]] bool zoomToExtents();
    [[nodiscard]] bool zoomToSelection();
    [[nodiscard]] bool selectAllDrawingItems();
    void setGridSnapEnabled(bool enabled);
    void setGridVisible(bool visible);
    void setObjectSnapEnabled(bool enabled);
    void setOrthoEnabled(bool enabled);
    void setPolarEnabled(bool enabled);
    [[nodiscard]] bool gridSnapEnabled() const noexcept;
    [[nodiscard]] bool gridVisible() const noexcept;
    [[nodiscard]] bool objectSnapEnabled() const noexcept;
    [[nodiscard]] bool orthoEnabled() const noexcept;
    [[nodiscard]] bool polarEnabled() const noexcept;
    [[nodiscard]] const commands::SelectionModel& selection() const noexcept;
    [[nodiscard]] const commands::PrecisionViewport& precisionViewport() const noexcept;
    [[nodiscard]] QWidget* interactionSurface() const noexcept;
    [[nodiscard]] quint64 dispatchedCanonicalEditCount() const noexcept;
    [[nodiscard]] bool
    requestEquipmentPlacement(const QString& catalogId, bool assembly, const QPointF& scenePoint);
    [[nodiscard]] QSize sizeHint() const override;

  protected:
    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void updatePointer(const QPointF& pixelPoint);
    void applyViewport();
    void rememberZoomView(const commands::PrecisionViewport& previous);
    [[nodiscard]] bool zoomToDrawingBounds(bool selectedOnly);
    bool completeCommand();
    [[nodiscard]] bool dispatchCanonicalEdit(const commands::CanonicalEditRequest& request);

    themes::ThemeTokens tokens_{themes::lightThemeTokens()};
    renderer::SceneSurface* sceneSurface_{nullptr};
    QWidget* interactionSurface_{nullptr};
    commands::PrecisionViewport precisionViewport_;
    QVector<commands::PrecisionViewport> zoomHistory_;
    commands::SnapSettings snapSettings_;
    commands::SnapResolver snapResolver_;
    commands::SnapResult pointerSnap_;
    commands::SelectionModel selection_;
    commands::DrawingCommandSession commandSession_;
    QVector<commands::SnapCandidate> electricalPortSnaps_;
    QHash<quint64, QString> semanticItemIds_;
    CanonicalEditHandler canonicalEditHandler_;
    InspectionSelectionHandler inspectionSelectionHandler_;
    std::function<void(const QString&)> commandInputHandler_;
    QString lastCompletedCommand_;
    bool canonicalEditConfirmationRequired_{false};
    std::optional<commands::DrawingCommandSession> submittedCommand_;
    QString submittedCommandId_;
    QPointF pointerPixel_;
    QPointF dragOriginPixel_;
    QPointF previousPanPixel_;
    QRectF marqueeSceneArea_;
    bool pointerAvailable_{false};
    bool marqueeActive_{false};
    bool panning_{false};
    bool spacePanEnabled_{false};
    bool spacePanUsed_{false};
    bool zoomOptionPending_{false};
    quint64 dispatchedCanonicalEditCount_{0};
};

class StudioDockWidget final : public QDockWidget {
  public:
    StudioDockWidget(QString panelId,
                     const QString& title,
                     QWidget* content,
                     QWidget* parent = nullptr);

    [[nodiscard]] QString panelId() const;
    [[nodiscard]] bool isPinned() const noexcept;
    [[nodiscard]] QAction* pinAction() const noexcept;
    [[nodiscard]] inspector::PanelState panelState() const;

    void setPinned(bool pinned);

  private:
    QString panelId_;
    bool pinned_{false};
    QAction* pinAction_{nullptr};
};

class WorkspaceSettings final {
  public:
    static constexpr int stateVersion = 1;

    explicit WorkspaceSettings(QSettings& settings) noexcept;

    [[nodiscard]] bool hasSavedLayout() const;
    [[nodiscard]] bool wasMaximized() const;
    [[nodiscard]] WorkspaceRestoreStatus restore(QMainWindow& window);
    void save(const QMainWindow& window);
    void clearLayout();
    [[nodiscard]] QHash<QString, QString> customCommandAliases() const;
    [[nodiscard]] bool saveCustomCommandAliases(const QHash<QString, QString>& aliases);

    [[nodiscard]] bool panelPinned(QStringView panelId) const;
    void savePanelPinned(QStringView panelId, bool pinned);

  private:
    QSettings& settings_;
};

class StudioMainWindow final : public QMainWindow {
  public:
    using InspectionIdentityResolver = std::function<std::optional<inspector::InspectionIdentity>(
        const QVector<canvas::SceneItemId>& selectedIds)>;

    StudioMainWindow(themes::ThemeController& themeController,
                     QSettings& settings,
                     QWidget* parent = nullptr);
    ~StudioMainWindow() override = default;

    [[nodiscard]] DrawingWorkspace* drawingWorkspace() const noexcept;
    [[nodiscard]] catalog::CatalogLibraryWidget* catalogLibrary() const noexcept;
    [[nodiscard]] inspector::SchemaInspectorWidget* schemaInspector() const noexcept;
    [[nodiscard]] QAction* commandAction(QStringView commandId) const;
    [[nodiscard]] StudioDockWidget* panel(QStringView panelId) const;
    [[nodiscard]] QList<StudioDockWidget*> panels() const;
    [[nodiscard]] QStringList menuTitles() const;
    [[nodiscard]] WorkspaceRestoreStatus restoreStatus() const noexcept;
    [[nodiscard]] bool shouldStartMaximized() const;

    void saveWorkspace();
    void resetWorkspace();
    void bindInspectionService(protocol::ServiceClient* client);
    void bindProjectService(protocol::ServiceClient* client);
    [[nodiscard]] bool openDrawingProject(const QString& path);
    [[nodiscard]] bool createDrawingProject(const QString& path, const QString& name);
    [[nodiscard]] bool saveDrawingProject();
    void bindSemanticEditService(protocol::ServiceClient* client,
                                 QString projectId,
                                 QString baseRevision);
    [[nodiscard]] QString semanticRevision() const;
    void setInspectionIdentityResolver(InspectionIdentityResolver resolver);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void configureWindow();
    void createMenus();
    void createPanels();
    void applyDefaultWorkspace();
    void restorePanelPins();
    void updateTheme();
    void updateThemeActions();
    void showAboutDialog();
    void showCommandAliases();
    void refreshDrawingAliasLabels();
    void handleProjectServiceUnavailable();

    [[nodiscard]] QMenu* menu(QStringView menuId) const;
    [[nodiscard]] QAction* registerAction(QString id,
                                          const QString& label,
                                          const QString& category,
                                          const QKeySequence& shortcut = {});
    [[nodiscard]] StudioDockWidget* addPanel(QString panelId,
                                             const QString& title,
                                             Qt::DockWidgetArea defaultArea,
                                             QWidget* content);
    [[nodiscard]] QWidget* createInformationPanel(const QString& title,
                                                  const QString& description) const;
    [[nodiscard]] QWidget* createCommandPanel();
    void addUnavailableAction(QMenu& target, const QString& explanation);
    [[nodiscard]] bool dispatchSemanticEdit(const commands::CanonicalEditRequest& request);
    [[nodiscard]] bool applyDrawingScene(const QJsonObject& payload);

    themes::ThemeController& themeController_;
    WorkspaceSettings workspaceSettings_;
    commands::CommandRegistry commandRegistry_;
    InspectionIdentityResolver inspectionIdentityResolver_;
    DrawingWorkspace* drawingWorkspace_{nullptr};
    catalog::CatalogLibraryWidget* catalogLibrary_{nullptr};
    inspector::SchemaInspectorWidget* schemaInspector_{nullptr};
    QLineEdit* commandLine_{nullptr};
    QActionGroup* themeActionGroup_{nullptr};
    QHash<QString, QMenu*> menus_;
    QHash<QString, QAction*> actions_;
    QHash<QString, StudioDockWidget*> panels_;
    QHash<QString, Qt::DockWidgetArea> defaultDockAreas_;
    protocol::ServiceClient* semanticEditClient_{nullptr};
    QString semanticProjectId_;
    QString semanticRevision_;
    QSet<QString> pendingSemanticRequests_;
    QPointer<protocol::ServiceClient> projectClient_;
    QString pendingProjectOpen_;
    QString pendingProjectSave_;
    QMetaObject::Connection projectResponseConnection_;
    QMetaObject::Connection projectStateConnection_;
    QMetaObject::Connection projectDestroyedConnection_;
    QMetaObject::Connection semanticResponseConnection_;
    QMetaObject::Connection semanticClientDestroyedConnection_;
    WorkspaceRestoreStatus restoreStatus_{WorkspaceRestoreStatus::NoSavedState};
};

} // namespace aimora::studio::shell
