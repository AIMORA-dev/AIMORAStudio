// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/inspector/inspection_document.hpp"

#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace aimora::studio::inspector {
namespace {

constexpr qsizetype maximumSections = 64;
constexpr qsizetype maximumFields = 4096;
constexpr qsizetype maximumTableRows = 100000;

void setError(QString* errorMessage, const QString& message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

[[nodiscard]] bool readRequiredString(const QJsonObject& object,
                                      const QString& key,
                                      QString* destination,
                                      QString* errorMessage) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("Inspector field '%1' is missing.").arg(key));
        return false;
    }
    *destination = value.toString();
    return true;
}

[[nodiscard]] QStringList stringArray(const QJsonValue& value, bool* valid) {
    QStringList result;
    if (value.isUndefined() || value.isNull()) {
        *valid = true;
        return result;
    }
    if (!value.isArray()) {
        *valid = false;
        return result;
    }
    QSet<QString> unique;
    for (const QJsonValue item : value.toArray()) {
        if (!item.isString() || item.toString().trimmed().isEmpty() ||
            unique.contains(item.toString())) {
            *valid = false;
            return {};
        }
        unique.insert(item.toString());
        result.push_back(item.toString());
    }
    *valid = true;
    return result;
}

[[nodiscard]] bool validRowArray(const QJsonValue& value) {
    if (!value.isArray() || value.toArray().size() > maximumTableRows) {
        return false;
    }
    QSet<QString> rowIds;
    for (const QJsonValue rowValue : value.toArray()) {
        if (!rowValue.isObject()) {
            return false;
        }
        const QString rowId = rowValue.toObject().value(QStringLiteral("row_id")).toString();
        if (rowId.trimmed().isEmpty() || rowIds.contains(rowId)) {
            return false;
        }
        rowIds.insert(rowId);
    }
    return true;
}

[[nodiscard]] bool
parseIdentity(const QJsonObject& root, InspectionIdentity* identity, QString* errorMessage) {
    const QJsonValue identityValue = root.value(QStringLiteral("identity"));
    if (!identityValue.isObject()) {
        setError(errorMessage, QStringLiteral("Inspector identity is missing."));
        return false;
    }
    const QJsonObject object = identityValue.toObject();
    if (!readRequiredString(
            object, QStringLiteral("project_id"), &identity->projectId, errorMessage) ||
        !readRequiredString(object, QStringLiteral("asset_id"), &identity->assetId, errorMessage) ||
        !readRequiredString(
            object, QStringLiteral("projection_id"), &identity->projectionId, errorMessage) ||
        !readRequiredString(object, QStringLiteral("view_id"), &identity->viewId, errorMessage)) {
        return false;
    }
    identity->equipmentClass = object.value(QStringLiteral("equipment_class")).toString();
    bool valid = false;
    identity->resultBindings = stringArray(object.value(QStringLiteral("result_bindings")), &valid);
    if (!valid) {
        setError(errorMessage, QStringLiteral("Inspector result bindings are invalid."));
        return false;
    }
    identity->assetIds = stringArray(object.value(QStringLiteral("asset_ids")), &valid);
    if (!valid) {
        setError(errorMessage, QStringLiteral("Inspector asset identities are invalid."));
        return false;
    }
    if (identity->assetIds.isEmpty()) {
        identity->assetIds = stringArray(root.value(QStringLiteral("asset_ids")), &valid);
        if (!valid) {
            setError(errorMessage, QStringLiteral("Inspector selection identities are invalid."));
            return false;
        }
    }
    if (identity->assetIds.isEmpty()) {
        identity->assetIds.push_back(identity->assetId);
    }
    return identity->isValid();
}

[[nodiscard]] bool
parseField(const QJsonObject& object, InspectionField* field, QString* errorMessage) {
    if (!readRequiredString(object, QStringLiteral("path"), &field->path, errorMessage) ||
        !readRequiredString(object, QStringLiteral("label"), &field->label, errorMessage) ||
        !readRequiredString(object, QStringLiteral("kind"), &field->kind, errorMessage)) {
        return false;
    }
    static const QSet<QString> kinds{
        QStringLiteral("boolean"),
        QStringLiteral("integer"),
        QStringLiteral("number"),
        QStringLiteral("text"),
        QStringLiteral("choice"),
        QStringLiteral("reference"),
        QStringLiteral("table"),
        QStringLiteral("curve"),
    };
    if (!kinds.contains(field->kind)) {
        setError(errorMessage,
                 QStringLiteral("Unknown inspector field kind '%1'.").arg(field->kind));
        return false;
    }
    field->dimension = object.value(QStringLiteral("dimension")).toString();
    field->canonicalUnit = object.value(QStringLiteral("canonical_unit")).toString();
    field->readOnly = object.value(QStringLiteral("read_only")).toBool(false);
    field->required = object.value(QStringLiteral("required")).toBool(false);
    field->provenance = object.value(QStringLiteral("provenance")).toString();
    if (object.value(QStringLiteral("range")).isObject()) {
        field->range = object.value(QStringLiteral("range")).toObject();
    }
    const QJsonValue uncertainty = object.value(QStringLiteral("uncertainty"));
    if (uncertainty.isDouble() && std::isfinite(uncertainty.toDouble())) {
        field->uncertainty = uncertainty.toDouble();
    }
    bool valid = false;
    field->dependencies = stringArray(object.value(QStringLiteral("dependencies")), &valid);
    if (!valid) {
        setError(errorMessage, QStringLiteral("Inspector field dependencies are invalid."));
        return false;
    }
    const QJsonValue unitsValue = object.value(QStringLiteral("display_units"));
    if (!unitsValue.isUndefined() && !unitsValue.isArray()) {
        setError(errorMessage, QStringLiteral("Inspector display units are invalid."));
        return false;
    }
    QSet<QString> unitSymbols;
    for (const QJsonValue unitValue : unitsValue.toArray()) {
        const QString symbol = unitValue.toObject().value(QStringLiteral("symbol")).toString();
        if (!unitValue.isObject() || symbol.trimmed().isEmpty() || unitSymbols.contains(symbol)) {
            setError(errorMessage, QStringLiteral("Inspector display unit is invalid."));
            return false;
        }
        unitSymbols.insert(symbol);
        field->displayUnits.push_back({symbol});
    }
    const QJsonValue choicesValue = object.value(QStringLiteral("choices"));
    if (!choicesValue.isUndefined() && !choicesValue.isArray()) {
        setError(errorMessage, QStringLiteral("Inspector choices are invalid."));
        return false;
    }
    for (const QJsonValue choiceValue : choicesValue.toArray()) {
        const QJsonObject choice = choiceValue.toObject();
        if (!choiceValue.isObject() || !choice.contains(QStringLiteral("value")) ||
            !choice.value(QStringLiteral("label")).isString()) {
            setError(errorMessage, QStringLiteral("Inspector choice is invalid."));
            return false;
        }
        field->choices.push_back({choice.value(QStringLiteral("value")),
                                  choice.value(QStringLiteral("label")).toString()});
    }
    return true;
}

} // namespace

bool InspectionIdentity::isValid() const {
    return !projectId.trimmed().isEmpty() && !assetId.trimmed().isEmpty() &&
           !projectionId.trimmed().isEmpty() && !viewId.trimmed().isEmpty() && !assetIds.isEmpty();
}

QJsonObject InspectionIdentity::toRequest() const {
    QJsonArray assets;
    for (const QString& id : assetIds) {
        assets.push_back(id);
    }
    QJsonObject request{
        {QStringLiteral("project_id"), projectId},
        {QStringLiteral("asset_id"), assetId},
        {QStringLiteral("projection_id"), projectionId},
        {QStringLiteral("view_id"), viewId},
    };
    if (assetIds.size() > 1) {
        request.insert(QStringLiteral("asset_ids"), assets);
    }
    return request;
}

std::optional<InspectionDocument> InspectionDocument::fromJson(const QJsonObject& object,
                                                               QString* errorMessage) {
    InspectionDocument document;
    if (!readRequiredString(
            object, QStringLiteral("schema_version"), &document.schemaVersion, errorMessage) ||
        !readRequiredString(object, QStringLiteral("revision"), &document.revision, errorMessage) ||
        !parseIdentity(object, &document.identity, errorMessage)) {
        return std::nullopt;
    }
    bool revisionValid = false;
    document.revision.toULongLong(&revisionValid);
    if (!revisionValid) {
        setError(errorMessage, QStringLiteral("Inspector revision is invalid."));
        return std::nullopt;
    }
    const QJsonValue sectionsValue = object.value(QStringLiteral("sections"));
    if (!sectionsValue.isArray() || sectionsValue.toArray().isEmpty() ||
        sectionsValue.toArray().size() > maximumSections) {
        setError(errorMessage, QStringLiteral("Inspector sections are invalid."));
        return std::nullopt;
    }
    qsizetype fieldCount = 0;
    QSet<QString> sectionIds;
    QSet<QString> fieldPaths;
    for (const QJsonValue sectionValue : sectionsValue.toArray()) {
        if (!sectionValue.isObject()) {
            setError(errorMessage, QStringLiteral("Inspector section is not an object."));
            return std::nullopt;
        }
        const QJsonObject sectionObject = sectionValue.toObject();
        InspectionSection section;
        if (!readRequiredString(sectionObject, QStringLiteral("id"), &section.id, errorMessage) ||
            !readRequiredString(
                sectionObject, QStringLiteral("title"), &section.title, errorMessage) ||
            sectionIds.contains(section.id)) {
            setError(errorMessage, QStringLiteral("Inspector section identity is invalid."));
            return std::nullopt;
        }
        sectionIds.insert(section.id);
        section.available = sectionObject.value(QStringLiteral("available")).toBool(true);
        section.unavailableReason =
            sectionObject.value(QStringLiteral("unavailable_reason")).toString();
        const QJsonValue fieldsValue = sectionObject.value(QStringLiteral("fields"));
        if (!fieldsValue.isArray() || (!section.available && !fieldsValue.toArray().isEmpty())) {
            setError(errorMessage, QStringLiteral("Inspector section fields are invalid."));
            return std::nullopt;
        }
        if (!section.available && section.unavailableReason.trimmed().isEmpty()) {
            setError(errorMessage, QStringLiteral("Unavailable inspector section has no reason."));
            return std::nullopt;
        }
        for (const QJsonValue fieldValue : fieldsValue.toArray()) {
            InspectionField field;
            if (!fieldValue.isObject() ||
                !parseField(fieldValue.toObject(), &field, errorMessage) ||
                fieldPaths.contains(field.path)) {
                setError(errorMessage, QStringLiteral("Inspector field identity is invalid."));
                return std::nullopt;
            }
            fieldPaths.insert(field.path);
            section.fields.push_back(std::move(field));
            ++fieldCount;
            if (fieldCount > maximumFields) {
                setError(errorMessage, QStringLiteral("Inspector field limit was exceeded."));
                return std::nullopt;
            }
        }
        document.sections.push_back(std::move(section));
    }
    const QJsonValue valuesValue = object.value(QStringLiteral("values"));
    if (!valuesValue.isObject()) {
        setError(errorMessage, QStringLiteral("Inspector values are missing."));
        return std::nullopt;
    }
    const QJsonObject values = valuesValue.toObject();
    for (auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator) {
        if (!fieldPaths.contains(iterator.key()) || !iterator.value().isObject()) {
            setError(errorMessage, QStringLiteral("Inspector value path is invalid."));
            return std::nullopt;
        }
        const QJsonObject valueObject = iterator.value().toObject();
        if (!valueObject.contains(QStringLiteral("value"))) {
            setError(errorMessage, QStringLiteral("Inspector value is missing."));
            return std::nullopt;
        }
        InspectionValue value{
            .value = valueObject.value(QStringLiteral("value")),
            .canonicalUnit = valueObject.value(QStringLiteral("canonical_unit")).toString(),
            .provenance = valueObject.value(QStringLiteral("provenance")).toString(),
            .issues = valueObject.value(QStringLiteral("issues")).toArray(),
            .mixed = valueObject.value(QStringLiteral("mixed")).toBool(false),
        };
        const auto section =
            std::find_if(document.sections.cbegin(),
                         document.sections.cend(),
                         [&](const InspectionSection& candidate) {
                             return std::any_of(candidate.fields.cbegin(),
                                                candidate.fields.cend(),
                                                [&](const InspectionField& field) {
                                                    return field.path == iterator.key();
                                                });
                         });
        const auto field = std::find_if(
            section->fields.cbegin(),
            section->fields.cend(),
            [&](const InspectionField& candidate) { return candidate.path == iterator.key(); });
        if ((field->kind == QStringLiteral("table") || field->kind == QStringLiteral("curve")) &&
            !value.mixed && !validRowArray(value.value)) {
            setError(errorMessage,
                     QStringLiteral("Inspector table rows require unique row_id values."));
            return std::nullopt;
        }
        document.values.insert(iterator.key(), std::move(value));
    }
    if (document.values.size() != fieldPaths.size()) {
        setError(errorMessage, QStringLiteral("Inspector values do not cover every field."));
        return std::nullopt;
    }
    document.undoAvailable = object.value(QStringLiteral("undo_available")).toBool(false);
    document.redoAvailable = object.value(QStringLiteral("redo_available")).toBool(false);
    document.selectionCount = object.value(QStringLiteral("selection_count")).toInteger(1);
    if (document.selectionCount < 1 ||
        document.selectionCount != document.identity.assetIds.size()) {
        setError(errorMessage, QStringLiteral("Inspector selection count is invalid."));
        return std::nullopt;
    }
    return document;
}

void InspectionDraft::reset(const InspectionDocument& document) {
    document_ = document;
    edits_.clear();
}

void InspectionDraft::clear() {
    document_.reset();
    edits_.clear();
}

void InspectionDraft::setEdit(QString path, QJsonValue value, QString displayUnit) {
    if (!document_.has_value() || !document_->values.contains(path)) {
        return;
    }
    const InspectionValue original = document_->values.value(path);
    if (!original.mixed && original.value == value &&
        (displayUnit.isEmpty() || displayUnit == original.canonicalUnit)) {
        edits_.remove(path);
        return;
    }
    edits_.insert(path, InspectionEdit{std::move(path), std::move(value), std::move(displayUnit)});
}

void InspectionDraft::removeEdit(const QString& path) {
    edits_.remove(path);
}

bool InspectionDraft::isDirty() const noexcept {
    return !edits_.isEmpty();
}

qsizetype InspectionDraft::size() const noexcept {
    return edits_.size();
}

QVector<InspectionEdit> InspectionDraft::edits() const {
    return QVector<InspectionEdit>{edits_.cbegin(), edits_.cend()};
}

QJsonObject InspectionDraft::commitRequest() const {
    if (!document_.has_value() || edits_.isEmpty()) {
        return {};
    }
    QJsonObject request = document_->identity.toRequest();
    request.remove(QStringLiteral("projection_id"));
    request.remove(QStringLiteral("view_id"));
    request.insert(QStringLiteral("base_revision"), document_->revision);
    QJsonArray edits;
    for (const InspectionEdit& edit : edits_) {
        QJsonObject item{{QStringLiteral("path"), edit.path},
                         {QStringLiteral("value"), edit.value}};
        if (!edit.displayUnit.isEmpty()) {
            item.insert(QStringLiteral("display_unit"), edit.displayUnit);
        }
        edits.push_back(item);
    }
    request.insert(QStringLiteral("edits"), edits);
    return request;
}

const InspectionDocument* InspectionDraft::document() const noexcept {
    return document_.has_value() ? &*document_ : nullptr;
}

QString inspectionJsonText(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', std::numeric_limits<double>::max_digits10);
    }
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    const QJsonDocument document =
        value.isArray() ? QJsonDocument{value.toArray()} : QJsonDocument{value.toObject()};
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

std::optional<QJsonValue> inspectionJsonValue(const QString& text, const QJsonValue& reference) {
    if (reference.isBool()) {
        if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
            return QJsonValue{true};
        }
        if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
            return QJsonValue{false};
        }
        return std::nullopt;
    }
    if (reference.isDouble()) {
        bool valid = false;
        const double value = text.toDouble(&valid);
        return valid && std::isfinite(value) ? std::optional<QJsonValue>{QJsonValue{value}}
                                             : std::nullopt;
    }
    if (reference.isArray() || reference.isObject()) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError ||
            (reference.isArray() && !document.isArray()) ||
            (reference.isObject() && !document.isObject())) {
            return std::nullopt;
        }
        return document.isArray() ? QJsonValue{document.array()} : QJsonValue{document.object()};
    }
    return QJsonValue{text};
}

} // namespace aimora::studio::inspector
