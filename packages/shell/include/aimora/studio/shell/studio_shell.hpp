// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/commands/command_registry.hpp"
#include "aimora/studio/commands/drawing_interaction.hpp"
#include "aimora/studio/inspector/panel_state.hpp"
#include "aimora/studio/renderer/scene_surface.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QDockWidget>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QMainWindow>
#include <QSettings>
#include <QStringList>
#include <QStringView>
#include <QVector>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <memory>

class QAction;
class QActionGroup;
class QCloseEvent;
class QEvent;
class QLineEdit;
class QMenu;

namespace aimora::studio::shell {

enum class WorkspaceRestoreStatus : std::uint8_t {
    Restored,
    NoSavedState,
    InvalidState,
};

class DrawingWorkspace final : public QWidget {
  public:
    using CanonicalEditHandler =
        std::function<bool(const commands::CanonicalEditRequest& request)>;

    explicit DrawingWorkspace(QWidget* parent = nullptr);

    void setScene(std::shared_ptr<const canvas::RetainedScene> scene);
    [[nodiscard]] renderer::SceneSurface* sceneSurface() const noexcept;
    void setThemeTokens(const themes::ThemeTokens& tokens);
    [[nodiscard]] const themes::ThemeTokens& themeTokens() const noexcept;
    void setElectricalPortSnaps(QVector<commands::SnapCandidate> ports);
    void setCanonicalEditHandler(CanonicalEditHandler handler);
    [[nodiscard]] bool executeCommandText(QStringView input);
    void setGridSnapEnabled(bool enabled);
    void setOrthoEnabled(bool enabled);
    void setPolarEnabled(bool enabled);
    [[nodiscard]] bool gridSnapEnabled() const noexcept;
    [[nodiscard]] bool orthoEnabled() const noexcept;
    [[nodiscard]] bool polarEnabled() const noexcept;
    [[nodiscard]] const commands::SelectionModel& selection() const noexcept;
    [[nodiscard]] const commands::PrecisionViewport& precisionViewport() const noexcept;
    [[nodiscard]] QWidget* interactionSurface() const noexcept;
    [[nodiscard]] quint64 dispatchedCanonicalEditCount() const noexcept;
    [[nodiscard]] QSize sizeHint() const override;

  protected:
    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void updatePointer(const QPointF& pixelPoint);
    void applyViewport();
    void completeCommand();

    themes::ThemeTokens tokens_{themes::lightThemeTokens()};
    renderer::SceneSurface* sceneSurface_{nullptr};
    QWidget* interactionSurface_{nullptr};
    commands::PrecisionViewport precisionViewport_;
    commands::SnapSettings snapSettings_;
    commands::SnapResolver snapResolver_;
    commands::SnapResult pointerSnap_;
    commands::SelectionModel selection_;
    commands::DrawingCommandSession commandSession_;
    QVector<commands::SnapCandidate> electricalPortSnaps_;
    CanonicalEditHandler canonicalEditHandler_;
    QPointF pointerPixel_;
    QPointF dragOriginPixel_;
    QPointF previousPanPixel_;
    QRectF marqueeSceneArea_;
    bool pointerAvailable_{false};
    bool marqueeActive_{false};
    bool panning_{false};
    bool spacePanEnabled_{false};
    quint64 dispatchedCanonicalEditCount_{0};
};

class StudioDockWidget final : public QDockWidget {
  public:
    StudioDockWidget(
        QString panelId,
        const QString& title,
        QWidget* content,
        QWidget* parent = nullptr
    );

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

    [[nodiscard]] bool panelPinned(QStringView panelId) const;
    void savePanelPinned(QStringView panelId, bool pinned);

  private:
    QSettings& settings_;
};

class StudioMainWindow final : public QMainWindow {
  public:
    StudioMainWindow(themes::ThemeController& themeController,
                     QSettings& settings,
                     QWidget* parent = nullptr);
    ~StudioMainWindow() override = default;

    [[nodiscard]] DrawingWorkspace* drawingWorkspace() const noexcept;
    [[nodiscard]] QAction* commandAction(QStringView commandId) const;
    [[nodiscard]] StudioDockWidget* panel(QStringView panelId) const;
    [[nodiscard]] QList<StudioDockWidget*> panels() const;
    [[nodiscard]] QStringList menuTitles() const;
    [[nodiscard]] WorkspaceRestoreStatus restoreStatus() const noexcept;
    [[nodiscard]] bool shouldStartMaximized() const;

    void saveWorkspace();
    void resetWorkspace();

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

    [[nodiscard]] QMenu* menu(QStringView menuId) const;
    [[nodiscard]] QAction*
    registerAction(
        QString id,
        const QString& label,
        const QString& category,
        const QKeySequence& shortcut = {}
    );
    [[nodiscard]] StudioDockWidget*
    addPanel(
        QString panelId,
        const QString& title,
        Qt::DockWidgetArea defaultArea,
        QWidget* content
    );
    [[nodiscard]] QWidget* createInformationPanel(
        const QString& title,
        const QString& description
    ) const;
    [[nodiscard]] QWidget* createCommandPanel();
    void addUnavailableAction(QMenu& target, const QString& explanation);

    themes::ThemeController& themeController_;
    WorkspaceSettings workspaceSettings_;
    commands::CommandRegistry commandRegistry_;
    DrawingWorkspace* drawingWorkspace_{nullptr};
    QLineEdit* commandLine_{nullptr};
    QActionGroup* themeActionGroup_{nullptr};
    QHash<QString, QMenu*> menus_;
    QHash<QString, QAction*> actions_;
    QHash<QString, StudioDockWidget*> panels_;
    QHash<QString, Qt::DockWidgetArea> defaultDockAreas_;
    WorkspaceRestoreStatus restoreStatus_{WorkspaceRestoreStatus::NoSavedState};
};

} // namespace aimora::studio::shell
