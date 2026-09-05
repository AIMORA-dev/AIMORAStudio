// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace aimora::studio::shell {

void StudioMainWindow::refreshDrawingAliasLabels() {
    const auto aliases = drawingWorkspace_->commandAliases();
    const QList<QPair<QString, QString>> commands{
        {QStringLiteral("draw.line"), tr("Line")},
        {QStringLiteral("draw.polyline"), tr("Polyline")},
        {QStringLiteral("draw.rectangle"), tr("Rectangle")},
        {QStringLiteral("draw.text"), tr("Text")},
        {QStringLiteral("draw.circle"), tr("Circle")},
        {QStringLiteral("draw.arc"), tr("Arc")},
        {QStringLiteral("draw.ellipse"), tr("Ellipse")},
        {QStringLiteral("modify.move"), tr("Move")},
        {QStringLiteral("modify.text"), tr("Edit Text")},
        {QStringLiteral("modify.explode_paths"), tr("Explode Drafting Paths")},
        {QStringLiteral("modify.join_lines"), tr("Join Drafting Lines")},
        {QStringLiteral("modify.mirror_horizontal"), tr("Mirror Horizontal")},
        {QStringLiteral("modify.mirror_vertical"), tr("Mirror Vertical")},
        {QStringLiteral("modify.rotate_quarter"), tr("Rotate 90 Degrees")},
        {QStringLiteral("modify.align_anchor_x"), tr("Align Anchors X")},
        {QStringLiteral("modify.align_anchor_y"), tr("Align Anchors Y")},
        {QStringLiteral("modify.distribute_anchor_x"), tr("Distribute Anchors X")},
        {QStringLiteral("modify.distribute_anchor_y"), tr("Distribute Anchors Y")},
        {QStringLiteral("modify.copy"), tr("Copy")},
        {QStringLiteral("modify.scale"), tr("Scale Geometry")},
        {QStringLiteral("modify.erase"), tr("Erase")},
    };
    for (const auto& [id, name] : commands) {
        QStringList names;
        for (auto iterator = aliases.cbegin(); iterator != aliases.cend(); ++iterator) {
            if (iterator.value() == id) {
                names.append(iterator.key());
            }
        }
        std::sort(names.begin(), names.end(), [](const QString& first, const QString& second) {
            return first.size() == second.size() ? first < second : first.size() < second.size();
        });
        if (QAction* action = commandAction(QStringView{id}); action != nullptr) {
            action->setText(names.isEmpty() ? name : tr("%1 (%2)").arg(name, names.first().toUpper()));
            action->setToolTip(tr("Command: %1\nAliases: %2").arg(id, names.join(QStringLiteral(", "))));
        }
    }
}

void StudioMainWindow::showCommandAliases() {
    QDialog dialog{this};
    dialog.setWindowTitle(tr("Command Aliases"));
    dialog.resize(600, 420);
    auto* layout = new QVBoxLayout{&dialog};
    auto* explanation = new QLabel{tr("Type an alias on the canvas or command line. Custom aliases override defaults; removing an override restores the default."), &dialog};
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* table = new QTableWidget{0, 2, &dialog};
    table->setAccessibleName(tr("Custom command aliases"));
    table->setHorizontalHeaderLabels({tr("Alias"), tr("Command")});
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
    const commands::DrawingCommandSession defaults;
    QStringList targets = defaults.commandAliases().values();
    targets.removeDuplicates();
    targets.sort();
    const auto append = [&](const QString& alias, const QString& target) {
        if (table->rowCount() >= 64) {
            return;
        }
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem{alias});
        auto* command = new QComboBox{table};
        command->setAccessibleName(tr("Target command"));
        command->setEditable(true);
        command->setInsertPolicy(QComboBox::NoInsert);
        command->addItems(targets);
        command->setCurrentText(target);
        table->setCellWidget(row, 1, command);
    };
    const auto saved = workspaceSettings_.customCommandAliases();
    QStringList aliases = saved.keys();
    aliases.sort();
    for (const QString& alias : aliases) {
        append(alias, saved.value(alias));
    }
    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Save | QDialogButtonBox::Cancel |
                                        QDialogButtonBox::RestoreDefaults, &dialog};
    QPushButton* add = buttons->addButton(tr("Add"), QDialogButtonBox::ActionRole);
    QPushButton* remove = buttons->addButton(tr("Remove"), QDialogButtonBox::ActionRole);
    connect(add, &QPushButton::clicked, &dialog, [&]() { append({}, QStringLiteral("draw.line")); });
    connect(remove, &QPushButton::clicked, &dialog, [&]() {
        if (table->currentRow() >= 0) {
            table->removeRow(table->currentRow());
        }
    });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            &dialog, [&]() { table->setRowCount(0); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        QHash<QString, QString> proposed;
        for (int row = 0; row < table->rowCount(); ++row) {
            const QString alias = table->item(row, 0)->text().trimmed().toLower();
            const auto* target = qobject_cast<QComboBox*>(table->cellWidget(row, 1));
            if (proposed.contains(alias) || target == nullptr) {
                QMessageBox::warning(&dialog, tr("Command Aliases"), tr("Each alias must be unique."));
                return;
            }
            proposed.insert(alias, target->currentText().trimmed().toLower());
        }
        if (!drawingWorkspace_->setCustomCommandAliases(proposed)) {
            QMessageBox::warning(&dialog, tr("Command Aliases"),
                tr("Finish or cancel the active command first. Aliases must start with a letter, use at most 16 letters/digits, avoid reserved navigation names, and target an existing command."));
            return;
        }
        refreshDrawingAliasLabels();
        if (!workspaceSettings_.saveCustomCommandAliases(proposed)) {
            QMessageBox::warning(&dialog, tr("Command Aliases"), tr("Aliases are active for this session but could not be saved to disk."));
            return;
        }
        dialog.accept();
    });
    layout->addWidget(buttons);
    dialog.exec();
}

} // namespace aimora::studio::shell
