// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <memory>
#include <optional>

namespace aimora::studio::catalog {

struct CatalogPart final {
    QString number;
    QString description;
    QString unit;
    int quantity{0};
};

struct CatalogCollection final {
    QString scope;
    bool mutableCollection{false};
    qsizetype count{0};
};

struct CatalogEntry final {
    QString kind;
    QString id;
    QString scope;
    QString category;
    QString label;
    QString description;
    QString equipmentClass;
    QString symbolId;
    QString designatorPrefix;
    QStringList keywords;
    QVector<CatalogPart> parts;
    qsizetype memberCount{0};
};

struct CatalogDocument final {
    QString schema;
    QString version;
    QString sourceOwner;
    QVector<CatalogCollection> collections;
    QVector<CatalogEntry> entries;

    [[nodiscard]] static std::optional<CatalogDocument>
    fromJson(const QJsonObject& object, QString* errorMessage = nullptr);
};

[[nodiscard]] QJsonObject bundledCatalogDocument(QString* errorMessage = nullptr);

class CatalogLibraryWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit CatalogLibraryWidget(QWidget* parent = nullptr);
    ~CatalogLibraryWidget() override;

    [[nodiscard]] bool setDocument(const QJsonObject& object);
    [[nodiscard]] qsizetype entryCount() const noexcept;
    [[nodiscard]] QStringList visibleEntryIds() const;
    [[nodiscard]] QString selectedEntryId() const;
    [[nodiscard]] QString statusMessage() const;

    void setSearchQuery(const QString& query);
    void setScopeFilter(const QString& scope);
    void setCategoryFilter(const QString& category);
    [[nodiscard]] bool selectEntry(const QString& catalogId);

  signals:
    void placementRequested(const QString& catalogId, bool assembly);

  private:
    struct State;
    std::unique_ptr<State> state_;

    void updateSelection();
    void updateStatus();
    void requestSelectedPlacement();
};

} // namespace aimora::studio::catalog
