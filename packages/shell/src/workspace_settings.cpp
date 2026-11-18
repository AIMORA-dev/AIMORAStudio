// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QByteArray>

namespace aimora::studio::shell {
namespace {

[[nodiscard]] QString settingsPanelKey(QStringView panelId) {
    return QStringLiteral("workspace/panels/%1/pinned").arg(panelId.toString());
}

} // namespace


WorkspaceSettings::WorkspaceSettings(QSettings& settings) noexcept
    : settings_{settings} {}

bool WorkspaceSettings::hasSavedLayout() const {
    return settings_.contains(QStringLiteral("workspace/geometry"))
        && settings_.contains(QStringLiteral("workspace/mainWindowState"));
}

bool WorkspaceSettings::wasMaximized() const {
    return settings_.value(QStringLiteral("workspace/maximized"), true).toBool();
}

WorkspaceRestoreStatus WorkspaceSettings::restore(QMainWindow& window) {
    const bool hasGeometry = settings_.contains(QStringLiteral("workspace/geometry"));
    const bool hasState = settings_.contains(QStringLiteral("workspace/mainWindowState"));
    if(!hasGeometry && !hasState) {
        return WorkspaceRestoreStatus::NoSavedState;
    }
    if(!hasGeometry || !hasState) {
        clearLayout();
        return WorkspaceRestoreStatus::InvalidState;
    }

    const QByteArray geometry =
        settings_.value(QStringLiteral("workspace/geometry")).toByteArray();
    const QByteArray state =
        settings_.value(QStringLiteral("workspace/mainWindowState")).toByteArray();

    const bool geometryRestored = !geometry.isEmpty() && window.restoreGeometry(geometry);
    const bool stateRestored =
        !state.isEmpty() && window.restoreState(state, stateVersion);
    if(!geometryRestored || !stateRestored) {
        clearLayout();
        return WorkspaceRestoreStatus::InvalidState;
    }
    return WorkspaceRestoreStatus::Restored;
}

void WorkspaceSettings::save(const QMainWindow& window) {
    settings_.setValue(QStringLiteral("workspace/geometry"), window.saveGeometry());
    settings_.setValue(
        QStringLiteral("workspace/mainWindowState"),
        window.saveState(stateVersion)
    );
    settings_.setValue(QStringLiteral("workspace/maximized"), window.isMaximized());
    settings_.sync();
}

void WorkspaceSettings::clearLayout() {
    settings_.remove(QStringLiteral("workspace"));
    settings_.sync();
}

bool WorkspaceSettings::panelPinned(QStringView panelId) const {
    return settings_.value(settingsPanelKey(panelId), false).toBool();
}

void WorkspaceSettings::savePanelPinned(QStringView panelId, bool pinned) {
    settings_.setValue(settingsPanelKey(panelId), pinned);
    settings_.sync();
}

} // namespace aimora::studio::shell
