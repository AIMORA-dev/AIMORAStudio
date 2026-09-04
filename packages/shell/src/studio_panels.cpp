// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QPair>
#include <QSignalBlocker>

namespace aimora::studio::shell {


void StudioMainWindow::createPanels() {
    QMenu* viewMenu = menu(QStringView{u"view"});
    QAction* panelInsertionAnchor = viewMenu->actions().front();

    StudioDockWidget* projectPanel = addPanel(
        QStringLiteral("panel.project-browser"),
        tr("Project Browser"),
        Qt::LeftDockWidgetArea,
        createInformationPanel(
            tr("Project Browser"),
            tr(
                "Project, drawing, study, result, and report trees will be populated "
                "from canonical Julia sessions."
            )
        )
    );
    QAction* projectAction = registerAction(
        QStringLiteral("view.project-browser"),
        tr("Project Browser"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("Ctrl+Shift+E")}
    );
    projectAction->setCheckable(true);
    viewMenu->insertAction(panelInsertionAnchor, projectAction);

    schemaInspector_ = new inspector::SchemaInspectorWidget;
    StudioDockWidget* inspectorPanel = addPanel(
        QStringLiteral("panel.inspector"),
        tr("Inspector"),
        Qt::RightDockWidgetArea,
        schemaInspector_
    );
    QAction* inspectorAction = registerAction(
        QStringLiteral("view.inspector"),
        tr("Inspector"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("F4")}
    );
    inspectorAction->setCheckable(true);
    viewMenu->insertAction(panelInsertionAnchor, inspectorAction);

    StudioDockWidget* commandPanel = addPanel(
        QStringLiteral("panel.command-line"),
        tr("Command Line"),
        Qt::BottomDockWidgetArea,
        createCommandPanel()
    );
    QAction* commandAction = registerAction(
        QStringLiteral("view.command-line"),
        tr("Command Line"),
        QStringLiteral("View"),
        QKeySequence{QStringLiteral("Ctrl+9")}
    );
    commandAction->setCheckable(true);
    viewMenu->insertAction(panelInsertionAnchor, commandAction);
    connect(commandPanel, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && commandLine_ != nullptr) {
            commandLine_->setFocus();
        }
    });
    viewMenu->insertSeparator(panelInsertionAnchor);

    const QList<QPair<QAction*, StudioDockWidget*>> panelBindings{
        {projectAction, projectPanel},
        {inspectorAction, inspectorPanel},
        {commandAction, commandPanel},
    };
    for(const auto& [toggleAction, dock] : panelBindings) {
        connect(toggleAction, &QAction::toggled, this, [dock, toggleAction](bool visible) {
            if(!visible && dock->isPinned()) {
                const QSignalBlocker blocker{toggleAction};
                toggleAction->setChecked(true);
                dock->show();
                dock->raise();
                return;
            }

            dock->setVisible(visible);
            if(visible) {
                dock->raise();
            }
        });
        connect(dock, &QDockWidget::visibilityChanged, this, [toggleAction](bool visible) {
            if(toggleAction->isChecked() != visible) {
                const QSignalBlocker blocker{toggleAction};
                toggleAction->setChecked(visible);
            }
        });
    }
}

} // namespace aimora::studio::shell
