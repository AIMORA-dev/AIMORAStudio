// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace aimora::studio::inspector {

struct InspectionIdentity final {
    QString projectId;
    QString assetId;
    QStringList assetIds;
    QString projectionId;
    QString viewId;
    QString equipmentClass;
    QStringList resultBindings;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QJsonObject toRequest() const;
};

struct InspectionUnit final {
    QString symbol;
};

struct InspectionChoice final {
    QJsonValue value;
    QString label;
};

struct InspectionField final {
    QString path;
    QString label;
    QString kind;
    QString dimension;
    QString canonicalUnit;
    QVector<InspectionUnit> displayUnits;
    QVector<InspectionChoice> choices;
    QJsonObject range;
    QStringList dependencies;
    bool readOnly{false};
    bool required{false};
    std::optional<double> uncertainty;
    QString provenance;
};

struct InspectionSection final {
    QString id;
    QString title;
    bool available{true};
    QString unavailableReason;
    QVector<InspectionField> fields;
};

struct InspectionValue final {
    QJsonValue value;
    QString canonicalUnit;
    QString provenance;
    QJsonArray issues;
    bool mixed{false};
};

struct InspectionDocument final {
    QString schemaVersion;
    QString revision;
    InspectionIdentity identity;
    QVector<InspectionSection> sections;
    QHash<QString, InspectionValue> values;
    bool undoAvailable{false};
    bool redoAvailable{false};
    qsizetype selectionCount{1};

    [[nodiscard]] static std::optional<InspectionDocument>
    fromJson(const QJsonObject& object, QString* errorMessage = nullptr);
};

struct InspectionEdit final {
    QString path;
    QJsonValue value;
    QString displayUnit;
};

class InspectionDraft final {
  public:
    void reset(const InspectionDocument& document);
    void clear();
    void setEdit(QString path, QJsonValue value, QString displayUnit = {});
    void removeEdit(const QString& path);

    [[nodiscard]] bool isDirty() const noexcept;
    [[nodiscard]] qsizetype size() const noexcept;
    [[nodiscard]] QVector<InspectionEdit> edits() const;
    [[nodiscard]] QJsonObject commitRequest() const;
    [[nodiscard]] const InspectionDocument* document() const noexcept;

  private:
    std::optional<InspectionDocument> document_;
    QMap<QString, InspectionEdit> edits_;
};

[[nodiscard]] QString inspectionJsonText(const QJsonValue& value);
[[nodiscard]] std::optional<QJsonValue> inspectionJsonValue(const QString& text,
                                                            const QJsonValue& reference = {});

} // namespace aimora::studio::inspector
