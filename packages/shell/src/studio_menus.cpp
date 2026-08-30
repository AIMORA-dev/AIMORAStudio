// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPair>

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
    menu(QStringView{u"edit"})->addAction(undoAction);

    QAction* redoAction = registerAction(
        QStringLiteral("edit.redo"),
        tr("Redo"),
        QStringLiteral("Edit"),
        QKeySequence::Redo
    );
    redoAction->setEnabled(false);
    menu(QStringView{u"edit"})->addAction(redoAction);

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

    addUnavailableAction(
        *menu(QStringView{u"draw"}),
        tr("Drawing commands are introduced by the precision viewport and drafting packets.")
    );
    addUnavailableAction(
        *menu(QStringView{u"modify"}),
        tr("Modify commands are introduced by the Power Drafting Profile packet.")
    );
    addUnavailableAction(
        *menu(QStringView{u"electrical"}),
        tr("Semantic equipment placement starts with the topology-safe SLD packet.")
    );
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
    addUnavailableAction(
        *menu(QStringView{u"tools"}),
        tr("Additional tools appear only when their executable capability is available.")
    );

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
