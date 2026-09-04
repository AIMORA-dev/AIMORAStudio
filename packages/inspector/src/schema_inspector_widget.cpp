// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/inspector/schema_inspector_widget.hpp"

#include "aimora/studio/protocol/generated/service_protocol.hpp"
#include "aimora/studio/protocol/service_client.hpp"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QStyle>
#include <QTableView>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#include <utility>

namespace aimora::studio::inspector {
namespace {

class JsonTableModel final : public QAbstractTableModel {
  public:
    explicit JsonTableModel(QJsonArray rows, QObject* parent = nullptr)
        : QAbstractTableModel{parent}, rows_{std::move(rows)} {
        QSet<QString> names;
        for (const QJsonValue row : rows_) {
            const QJsonObject object = row.toObject();
            for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
                names.insert(iterator.key());
            }
        }
        names.remove(QStringLiteral("row_id"));
        columns_ = names.values();
        std::sort(columns_.begin(), columns_.end());
        columns_.prepend(QStringLiteral("row_id"));
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(columns_.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= rows_.size() || index.column() >= columns_.size()) {
            return {};
        }
        const QJsonValue value =
            rows_.at(index.row()).toObject().value(columns_.at(index.column()));
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            return inspectionJsonText(value);
        }
        if (role == Qt::ToolTipRole && index.column() == 0) {
            return tr("Stable Julia-owned row identity");
        }
        return {};
    }

    [[nodiscard]] QVariant
    headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole) {
            return {};
        }
        if (orientation == Qt::Vertical) {
            return section + 1;
        }
        return section >= 0 && section < columns_.size() ? columns_.at(section) : QVariant{};
    }

    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override {
        Qt::ItemFlags result = QAbstractTableModel::flags(index);
        if (index.isValid() && index.column() != 0 && editable_) {
            result |= Qt::ItemIsEditable;
        }
        return result;
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role) override {
        if (role != Qt::EditRole || !index.isValid() || index.column() == 0 || !editable_) {
            return false;
        }
        QJsonObject row = rows_.at(index.row()).toObject();
        const QString column = columns_.at(index.column());
        const auto parsed = inspectionJsonValue(value.toString(), row.value(column));
        if (!parsed.has_value()) {
            return false;
        }
        row.insert(column, *parsed);
        rows_.replace(index.row(), row);
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    void setEditable(bool editable) {
        editable_ = editable;
    }

    void appendRow() {
        if (!editable_) {
            return;
        }
        QJsonObject row;
        row.insert(
            QStringLiteral("row_id"),
            QStringLiteral("draft-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        for (const QString& column : columns_) {
            if (column != QStringLiteral("row_id")) {
                row.insert(column, QString{});
            }
        }
        const int position = static_cast<int>(rows_.size());
        beginInsertRows({}, position, position);
        rows_.push_back(row);
        endInsertRows();
    }

    void removeRowAt(int row) {
        if (!editable_ || row < 0 || row >= rows_.size()) {
            return;
        }
        beginRemoveRows({}, row, row);
        rows_.removeAt(row);
        endRemoveRows();
    }

    [[nodiscard]] QJsonArray rows() const {
        return rows_;
    }

  private:
    QJsonArray rows_;
    QStringList columns_;
    bool editable_{true};
};

enum class RequestOperation : std::uint8_t {
    Describe,
    Commit,
    Undo,
    Redo,
};

struct EditorBinding final {
    InspectionField field;
    QPointer<QWidget> editor;
    QPointer<QComboBox> unit;
    QPointer<JsonTableModel> table;
};

[[nodiscard]] QStringList jsonStringArray(const QJsonValue& value) {
    QStringList result;
    for (const QJsonValue item : value.toArray()) {
        if (item.isString()) {
            result.push_back(item.toString());
        }
    }
    return result;
}

void clearLayout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (item->layout() != nullptr) {
            clearLayout(item->layout());
            delete item->layout();
        }
        delete item->widget();
        delete item;
    }
}

[[nodiscard]] QString issueText(const InspectionValue& value) {
    QStringList lines;
    if (!value.provenance.trimmed().isEmpty()) {
        lines.push_back(QObject::tr("Source: %1").arg(value.provenance));
    }
    for (const QJsonValue issueValue : value.issues) {
        const QJsonObject issue = issueValue.toObject();
        const QString message = issue.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            lines.push_back(message);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

struct SchemaInspectorWidget::State final {
    QPointer<protocol::ServiceClient> client;
    QMetaObject::Connection responseConnection;
    QMetaObject::Connection detailsConnection;
    QHash<QString, RequestOperation> pending;
    QHash<QString, QJsonObject> failureDetails;
    std::optional<InspectionIdentity> requestedIdentity;
    std::optional<InspectionDocument> document;
    InspectionDraft draft;
    QHash<QString, EditorBinding> editors;
    QPointer<QLabel> identityLabel;
    QPointer<QLabel> statusLabel;
    QPointer<QWidget> sectionContainer;
    QPointer<QVBoxLayout> sectionLayout;
    QPointer<QPushButton> applyButton;
    QPointer<QPushButton> revertButton;
    QPointer<QPushButton> reloadButton;
    QPointer<QPushButton> undoButton;
    QPointer<QPushButton> redoButton;
    bool stale{false};
};

SchemaInspectorWidget::SchemaInspectorWidget(QWidget* parent)
    : QWidget{parent}, state_{std::make_unique<State>()} {
    setObjectName(QStringLiteral("aimora.schema-inspector"));
    setAccessibleName(tr("Equipment inspector"));
    setProperty("aimoraPanel", true);

    auto* rootLayout = new QVBoxLayout{this};
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(8);

    state_->identityLabel = new QLabel{tr("No Selection"), this};
    QFont identityFont = state_->identityLabel->font();
    identityFont.setBold(true);
    state_->identityLabel->setFont(identityFont);
    state_->identityLabel->setObjectName(QStringLiteral("aimora.inspector.identity"));
    rootLayout->addWidget(state_->identityLabel);

    state_->statusLabel =
        new QLabel{tr("Select a semantic symbol to request its Julia-owned schema."), this};
    state_->statusLabel->setWordWrap(true);
    state_->statusLabel->setTextInteractionFlags(Qt::TextSelectableByKeyboard);
    state_->statusLabel->setObjectName(QStringLiteral("aimora.inspector.status"));
    rootLayout->addWidget(state_->statusLabel);

    auto* scrollArea = new QScrollArea{this};
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    state_->sectionContainer = new QWidget{scrollArea};
    state_->sectionLayout = new QVBoxLayout{state_->sectionContainer};
    state_->sectionLayout->setContentsMargins(0, 0, 0, 0);
    state_->sectionLayout->addStretch(1);
    scrollArea->setWidget(state_->sectionContainer);
    rootLayout->addWidget(scrollArea, 1);

    auto* actions = new QHBoxLayout;
    state_->applyButton = new QPushButton{tr("Apply"), this};
    state_->revertButton = new QPushButton{tr("Revert"), this};
    state_->reloadButton = new QPushButton{tr("Reload"), this};
    state_->undoButton = new QPushButton{tr("Undo"), this};
    state_->redoButton = new QPushButton{tr("Redo"), this};
    state_->applyButton->setObjectName(QStringLiteral("aimora.inspector.apply"));
    state_->revertButton->setObjectName(QStringLiteral("aimora.inspector.revert"));
    state_->reloadButton->setObjectName(QStringLiteral("aimora.inspector.reload"));
    state_->undoButton->setObjectName(QStringLiteral("aimora.inspector.undo"));
    state_->redoButton->setObjectName(QStringLiteral("aimora.inspector.redo"));
    for (QPushButton* button : {state_->undoButton.data(),
                                state_->redoButton.data(),
                                state_->reloadButton.data(),
                                state_->revertButton.data(),
                                state_->applyButton.data()}) {
        actions->addWidget(button);
    }
    rootLayout->addLayout(actions);

    connect(state_->applyButton, &QPushButton::clicked, this, &SchemaInspectorWidget::submitCommit);
    connect(state_->revertButton, &QPushButton::clicked, this, [this]() {
        if (state_->document.has_value()) {
            state_->draft.reset(*state_->document);
            state_->stale = false;
            renderDocument();
            setStatus(tr("Local changes were reverted."));
        }
    });
    connect(state_->reloadButton,
            &QPushButton::clicked,
            this,
            &SchemaInspectorWidget::reloadAuthoritative);
    connect(state_->undoButton, &QPushButton::clicked, this, [this]() { submitHistory(true); });
    connect(state_->redoButton, &QPushButton::clicked, this, [this]() { submitHistory(false); });
    updateActions();
}

SchemaInspectorWidget::~SchemaInspectorWidget() = default;

void SchemaInspectorWidget::bindServiceClient(protocol::ServiceClient* client) {
    if (state_->client == client) {
        return;
    }
    QObject::disconnect(state_->responseConnection);
    QObject::disconnect(state_->detailsConnection);
    state_->client = client;
    state_->pending.clear();
    state_->failureDetails.clear();
    if (client == nullptr) {
        setStatus(tr("The Julia inspection service is disconnected."), true);
        updateActions();
        return;
    }
    state_->detailsConnection =
        connect(client,
                &protocol::ServiceClient::responseFailureDetailsReceived,
                this,
                [this](const QString& requestId, const QJsonObject& details) {
                    state_->failureDetails.insert(requestId, details);
                });
    state_->responseConnection =
        connect(client,
                &protocol::ServiceClient::responseReceived,
                this,
                [this](const QString& requestId,
                       bool ok,
                       const QJsonObject& result,
                       const QString& errorCode,
                       const QString& errorMessage) {
                    handleResponse(requestId, ok, result, errorCode, errorMessage);
                });
    setStatus(client->isReady() ? tr("Julia inspection service is ready.")
                                : tr("Waiting for the Julia inspection service."));
    updateActions();
}

void SchemaInspectorWidget::inspect(const InspectionIdentity& identity) {
    if (!identity.isValid()) {
        setStatus(tr("The selected semantic identity is incomplete."), true);
        return;
    }
    state_->requestedIdentity = identity;
    state_->document.reset();
    state_->draft.clear();
    state_->stale = false;
    renderDocument();
    reloadAuthoritative();
}

bool SchemaInspectorWidget::setDocument(const QJsonObject& object) {
    QString errorMessage;
    auto parsed = InspectionDocument::fromJson(object, &errorMessage);
    if (!parsed.has_value()) {
        setStatus(errorMessage, true);
        return false;
    }
    state_->document = std::move(*parsed);
    state_->requestedIdentity = state_->document->identity;
    state_->draft.reset(*state_->document);
    state_->stale = false;
    renderDocument();
    setStatus(tr("Authoritative revision %1 loaded.").arg(state_->document->revision));
    return true;
}

void SchemaInspectorWidget::clearInspection() {
    state_->requestedIdentity.reset();
    state_->document.reset();
    state_->draft.clear();
    state_->stale = false;
    renderDocument();
    setStatus(tr("Select a semantic symbol to request its Julia-owned schema."));
}

const InspectionDocument* SchemaInspectorWidget::document() const noexcept {
    return state_->document.has_value() ? &*state_->document : nullptr;
}

bool SchemaInspectorWidget::hasLocalEdits() const noexcept {
    return state_->draft.isDirty();
}

QJsonObject SchemaInspectorWidget::pendingCommitRequest() const {
    return state_->draft.commitRequest();
}

QString SchemaInspectorWidget::statusMessage() const {
    return state_->statusLabel != nullptr ? state_->statusLabel->text() : QString{};
}

void SchemaInspectorWidget::renderDocument() {
    state_->editors.clear();
    clearLayout(state_->sectionLayout);
    if (!state_->document.has_value()) {
        state_->identityLabel->setText(tr("No Selection"));
        state_->sectionLayout->addStretch(1);
        updateActions();
        return;
    }
    const InspectionDocument& document = *state_->document;
    state_->identityLabel->setText(
        document.selectionCount > 1
            ? tr("%1 items - %2").arg(document.selectionCount).arg(document.identity.equipmentClass)
            : tr("%1 - %2").arg(document.identity.assetId, document.identity.equipmentClass));

    for (const InspectionSection& section : document.sections) {
        auto* group = new QGroupBox{section.title, state_->sectionContainer};
        group->setObjectName(QStringLiteral("aimora.inspector.section.%1").arg(section.id));
        group->setAccessibleName(section.title);
        auto* form = new QFormLayout{group};
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        if (!section.available) {
            auto* unavailable = new QLabel{section.unavailableReason, group};
            unavailable->setWordWrap(true);
            unavailable->setProperty("aimoraUnavailable", true);
            form->addRow(unavailable);
        }
        for (const InspectionField& field : section.fields) {
            const InspectionValue value = document.values.value(field.path);
            auto* editorContainer = new QWidget{group};
            auto* editorLayout = new QHBoxLayout{editorContainer};
            editorLayout->setContentsMargins(0, 0, 0, 0);
            EditorBinding binding{.field = field};

            if (field.kind == QStringLiteral("boolean")) {
                auto* editor = new QCheckBox{editorContainer};
                editor->setChecked(value.value.toBool());
                editor->setTristate(value.mixed);
                if (value.mixed) {
                    editor->setCheckState(Qt::PartiallyChecked);
                }
                binding.editor = editor;
                editorLayout->addWidget(editor);
                connect(editor,
                        &QCheckBox::checkStateChanged,
                        this,
                        [this, path = field.path](Qt::CheckState) { stageScalar(path); });
            } else if (field.kind == QStringLiteral("choice")) {
                auto* editor = new QComboBox{editorContainer};
                if (value.mixed) {
                    editor->addItem(tr("Mixed values"), QVariant{});
                }
                for (const InspectionChoice& choice : field.choices) {
                    editor->addItem(choice.label, choice.value.toVariant());
                    if (!value.mixed && choice.value == value.value) {
                        editor->setCurrentIndex(editor->count() - 1);
                    }
                }
                binding.editor = editor;
                editorLayout->addWidget(editor, 1);
                connect(editor,
                        &QComboBox::currentIndexChanged,
                        this,
                        [this, path = field.path](int) { stageScalar(path); });
            } else if (field.kind == QStringLiteral("table") ||
                       field.kind == QStringLiteral("curve")) {
                auto* table = new QTableView{editorContainer};
                auto* model = new JsonTableModel{value.value.toArray(), table};
                model->setEditable(!field.readOnly);
                table->setModel(model);
                table->setSelectionBehavior(QAbstractItemView::SelectRows);
                table->setSelectionMode(QAbstractItemView::SingleSelection);
                table->horizontalHeader()->setStretchLastSection(true);
                table->verticalHeader()->setVisible(false);
                table->setMinimumHeight(150);
                binding.editor = table;
                binding.table = model;
                auto* tableColumn = new QVBoxLayout;
                tableColumn->addWidget(table);
                if (!field.readOnly) {
                    auto* rowActions = new QHBoxLayout;
                    auto* addRow = new QPushButton{tr("Add row"), editorContainer};
                    auto* removeRow = new QPushButton{tr("Remove row"), editorContainer};
                    addRow->setAccessibleName(tr("Add row to %1").arg(field.label));
                    removeRow->setAccessibleName(
                        tr("Remove selected row from %1").arg(field.label));
                    rowActions->addWidget(addRow);
                    rowActions->addWidget(removeRow);
                    rowActions->addStretch(1);
                    tableColumn->addLayout(rowActions);
                    connect(
                        addRow, &QPushButton::clicked, this, [this, model, path = field.path]() {
                            model->appendRow();
                            stageTable(path);
                        });
                    connect(removeRow,
                            &QPushButton::clicked,
                            this,
                            [this, model, table, path = field.path]() {
                                model->removeRowAt(table->currentIndex().row());
                                stageTable(path);
                            });
                }
                editorLayout->addLayout(tableColumn, 1);
                connect(model, &QAbstractItemModel::dataChanged, this, [this, path = field.path]() {
                    stageTable(path);
                });
            } else {
                auto* editor = new QLineEdit{editorContainer};
                editor->setText(value.mixed ? QString{} : inspectionJsonText(value.value));
                if (value.mixed) {
                    editor->setPlaceholderText(tr("Mixed values"));
                }
                binding.editor = editor;
                editorLayout->addWidget(editor, 1);
                connect(editor, &QLineEdit::editingFinished, this, [this, path = field.path]() {
                    stageScalar(path);
                });
            }

            if (!field.canonicalUnit.isEmpty()) {
                auto* unit = new QComboBox{editorContainer};
                QSet<QString> symbols;
                unit->addItem(field.canonicalUnit);
                symbols.insert(field.canonicalUnit);
                for (const InspectionUnit& candidate : field.displayUnits) {
                    if (!symbols.contains(candidate.symbol)) {
                        unit->addItem(candidate.symbol);
                        symbols.insert(candidate.symbol);
                    }
                }
                unit->setAccessibleName(tr("Display unit for %1").arg(field.label));
                binding.unit = unit;
                unit->setEnabled(!field.readOnly);
                editorLayout->addWidget(unit);
                connect(unit,
                        &QComboBox::currentIndexChanged,
                        this,
                        [this, path = field.path](int) { stageScalar(path); });
            }

            binding.editor->setObjectName(
                QStringLiteral("aimora.inspector.field.%1").arg(field.path));
            binding.editor->setAccessibleName(field.label);
            binding.editor->setEnabled(!field.readOnly);
            const QString details = issueText(value);
            if (!details.isEmpty()) {
                binding.editor->setToolTip(details);
                binding.editor->setAccessibleDescription(details);
            }
            state_->editors.insert(field.path, binding);
            form->addRow(field.label, editorContainer);
        }
        state_->sectionLayout->addWidget(group);
    }
    state_->sectionLayout->addStretch(1);
    updateActions();
}

void SchemaInspectorWidget::updateActions() {
    const bool available = state_->document.has_value();
    const bool pending = !state_->pending.isEmpty();
    const bool connected = state_->client != nullptr && state_->client->isReady();
    state_->applyButton->setEnabled(available && connected && state_->draft.isDirty() &&
                                    !state_->stale && !pending);
    state_->revertButton->setEnabled(available && state_->draft.isDirty() && !pending);
    state_->reloadButton->setEnabled(state_->requestedIdentity.has_value() && connected &&
                                     !pending);
    state_->undoButton->setEnabled(available && connected && state_->document->undoAvailable &&
                                   !state_->draft.isDirty() && !pending);
    state_->redoButton->setEnabled(available && connected && state_->document->redoAvailable &&
                                   !state_->draft.isDirty() && !pending);
}

void SchemaInspectorWidget::setStatus(const QString& message, bool error) {
    state_->statusLabel->setText(message);
    state_->statusLabel->setProperty("aimoraError", error);
    state_->statusLabel->setAccessibleDescription(message);
    state_->statusLabel->style()->unpolish(state_->statusLabel);
    state_->statusLabel->style()->polish(state_->statusLabel);
}

void SchemaInspectorWidget::stageScalar(const QString& path) {
    const auto iterator = state_->editors.constFind(path);
    if (iterator == state_->editors.cend() || iterator->field.readOnly ||
        !state_->document.has_value()) {
        return;
    }
    QJsonValue edited;
    if (auto* checkBox = qobject_cast<QCheckBox*>(iterator->editor.data())) {
        if (checkBox->checkState() == Qt::PartiallyChecked) {
            return;
        }
        edited = checkBox->isChecked();
    } else if (auto* combo = qobject_cast<QComboBox*>(iterator->editor.data())) {
        if (!combo->currentData().isValid()) {
            return;
        }
        edited = QJsonValue::fromVariant(combo->currentData());
    } else if (auto* lineEdit = qobject_cast<QLineEdit*>(iterator->editor.data())) {
        const QJsonValue reference = state_->document->values.value(path).value;
        const auto parsed = inspectionJsonValue(lineEdit->text(), reference);
        if (!parsed.has_value()) {
            setStatus(tr("%1 contains an invalid value.").arg(iterator->field.label), true);
            return;
        }
        edited = *parsed;
    } else {
        return;
    }
    const QString unit = iterator->unit != nullptr ? iterator->unit->currentText() : QString{};
    state_->draft.setEdit(path, edited, unit);
    setStatus(tr("%1 local change(s) pending Julia validation.").arg(state_->draft.size()));
    updateActions();
}

void SchemaInspectorWidget::stageTable(const QString& path) {
    const auto iterator = state_->editors.constFind(path);
    if (iterator == state_->editors.cend() || iterator->table == nullptr ||
        iterator->field.readOnly) {
        return;
    }
    state_->draft.setEdit(path, iterator->table->rows());
    setStatus(tr("%1 local change(s) pending Julia validation.").arg(state_->draft.size()));
    updateActions();
}

void SchemaInspectorWidget::submitCommit() {
    if (state_->client == nullptr || !state_->client->isReady() || state_->stale) {
        return;
    }
    const QJsonObject request = state_->draft.commitRequest();
    if (request.isEmpty()) {
        return;
    }
    const QString requestId =
        state_->client->sendRequest(protocol::generated::Method::InspectorCommit, request);
    if (requestId.isEmpty()) {
        setStatus(tr("The inspector transaction could not be sent."), true);
        return;
    }
    state_->pending.insert(requestId, RequestOperation::Commit);
    setStatus(tr("Validating transaction in Julia..."));
    updateActions();
}

void SchemaInspectorWidget::submitHistory(bool undo) {
    if (state_->client == nullptr || !state_->client->isReady() || !state_->document.has_value() ||
        state_->draft.isDirty()) {
        return;
    }
    QJsonObject request = state_->document->identity.toRequest();
    request.remove(QStringLiteral("projection_id"));
    request.remove(QStringLiteral("view_id"));
    request.insert(QStringLiteral("base_revision"), state_->document->revision);
    const auto method = undo ? protocol::generated::Method::InspectorUndo
                             : protocol::generated::Method::InspectorRedo;
    const QString requestId = state_->client->sendRequest(method, request);
    if (requestId.isEmpty()) {
        setStatus(tr("The inspector history request could not be sent."), true);
        return;
    }
    state_->pending.insert(requestId, undo ? RequestOperation::Undo : RequestOperation::Redo);
    setStatus(undo ? tr("Undoing in Julia...") : tr("Redoing in Julia..."));
    updateActions();
}

void SchemaInspectorWidget::reloadAuthoritative() {
    if (state_->client == nullptr || !state_->client->isReady() ||
        !state_->requestedIdentity.has_value()) {
        setStatus(tr("The Julia inspection service is not ready."), true);
        return;
    }
    const QString requestId = state_->client->sendRequest(
        protocol::generated::Method::InspectorDescribe, state_->requestedIdentity->toRequest());
    if (requestId.isEmpty()) {
        setStatus(tr("The inspector schema request could not be sent."), true);
        return;
    }
    state_->pending.insert(requestId, RequestOperation::Describe);
    setStatus(tr("Loading authoritative inspector schema..."));
    updateActions();
}

void SchemaInspectorWidget::handleResponse(const QString& requestId,
                                           bool ok,
                                           const QJsonObject& result,
                                           const QString& errorCode,
                                           const QString& errorMessage) {
    const auto iterator = state_->pending.find(requestId);
    if (iterator == state_->pending.end()) {
        return;
    }
    const RequestOperation operation = iterator.value();
    state_->pending.erase(iterator);
    const QJsonObject details = state_->failureDetails.take(requestId);
    if (!ok) {
        QString message = errorMessage;
        const QJsonArray issues = details.value(QStringLiteral("issues")).toArray();
        for (const QJsonValue issueValue : issues) {
            const QString issue = issueValue.toObject().value(QStringLiteral("message")).toString();
            if (!issue.isEmpty()) {
                message += QStringLiteral("\n") + issue;
            }
        }
        if (errorCode == QStringLiteral("REVISION_CONFLICT")) {
            state_->stale = true;
            message += tr("\nLocal edits were preserved. Reload before applying again.");
        }
        setStatus(QStringLiteral("%1: %2").arg(errorCode, message), true);
        updateActions();
        return;
    }
    if (operation == RequestOperation::Describe) {
        setDocument(result);
        return;
    }
    const QString status = result.value(QStringLiteral("status")).toString();
    if (status != QStringLiteral("accepted")) {
        setStatus(tr("Julia returned inspector status '%1'.").arg(status), true);
        updateActions();
        return;
    }
    emit affectedViewsChanged(
        jsonStringArray(result.value(QStringLiteral("affected_model_paths"))),
        jsonStringArray(result.value(QStringLiteral("affected_view_ids"))),
        jsonStringArray(result.value(QStringLiteral("invalidated_result_ids"))));
    state_->draft.clear();
    state_->stale = false;
    reloadAuthoritative();
}

} // namespace aimora::studio::inspector
