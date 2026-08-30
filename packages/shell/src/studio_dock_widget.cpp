// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QAction>
#include <QSignalBlocker>

#include <utility>

namespace aimora::studio::shell {


StudioDockWidget::StudioDockWidget(
    QString panelId,
    QString title,
    QWidget* content,
    QWidget* parent
)
    : QDockWidget{std::move(title), parent},
      panelId_{std::move(panelId)} {
    setObjectName(panelId_);
    setAccessibleName(windowTitle());
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(
        QDockWidget::DockWidgetClosable
        | QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable
    );
    setWidget(content);

    pinAction_ = new QAction{tr("Pin panel"), this};
    pinAction_->setObjectName(panelId_ + QStringLiteral(".pin"));
    pinAction_->setCheckable(true);
    pinAction_->setToolTip(tr("Keep this panel open until it is unpinned."));
    addAction(pinAction_);
    setContextMenuPolicy(Qt::ActionsContextMenu);

    connect(pinAction_, &QAction::toggled, this, [this](bool pinned) {
        setPinned(pinned);
    });
}

QString StudioDockWidget::panelId() const {
    return panelId_;
}

bool StudioDockWidget::isPinned() const noexcept {
    return pinned_;
}

QAction* StudioDockWidget::pinAction() const noexcept {
    return pinAction_;
}

inspector::PanelState StudioDockWidget::panelState() const {
    inspector::PanelPresentation presentation = inspector::PanelPresentation::Hidden;
    if(isVisible()) {
        presentation = isFloating()
            ? inspector::PanelPresentation::Floating
            : inspector::PanelPresentation::Docked;
    }

    return inspector::PanelState{
        .presentation = presentation,
        .pinned = pinned_,
        .preferredSize = size(),
    };
}

void StudioDockWidget::setPinned(bool pinned) {
    pinned_ = pinned;

    QDockWidget::DockWidgetFeatures updated = features();
    updated.setFlag(QDockWidget::DockWidgetClosable, !pinned_);
    if(pinned_) {
        show();
        raise();
    }
    setFeatures(updated);

    if(pinAction_->isChecked() != pinned_) {
        const QSignalBlocker blocker{pinAction_};
        pinAction_->setChecked(pinned_);
    }
}

} // namespace aimora::studio::shell
