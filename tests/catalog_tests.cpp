// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/catalog/catalog_library.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace aimora::studio::catalog;

namespace {

constexpr auto catalogMimeType = "application/vnd.aimora.catalog-entry+json";
const QString breakerId =
    QStringLiteral("aimora://catalog/system/switching.circuit_breaker@1.0.0");

} // namespace

class CatalogTests final : public QObject {
    Q_OBJECT

  private slots:
    void parsesCompleteBundledCatalog();
    void searchesFiltersAndRequestsStableIds();
    void publishesStableDragPayload();
    void rejectsDuplicateEntries();
};

void CatalogTests::parsesCompleteBundledCatalog() {
    QString error;
    const QJsonObject object = bundledCatalogDocument(&error);
    QVERIFY2(!object.isEmpty(), qPrintable(error));
    const auto document = CatalogDocument::fromJson(object, &error);
    QVERIFY2(document.has_value(), qPrintable(error));
    QCOMPARE(document->entries.size(), 16);
    QCOMPARE(document->collections.size(), 3);

    QSet<QString> categories;
    qsizetype assemblyCount = 0;
    for(const CatalogEntry& entry : document->entries) {
        categories.insert(entry.category);
        assemblyCount += entry.kind == QStringLiteral("assembly") ? 1 : 0;
        QVERIFY(!entry.parts.isEmpty());
    }
    QCOMPARE(categories.size(), 13);
    QCOMPARE(assemblyCount, 2);
}

void CatalogTests::searchesFiltersAndRequestsStableIds() {
    CatalogLibraryWidget widget;
    QCOMPARE(widget.entryCount(), 16);
    QVERIFY(widget.findChild<QLineEdit*>(QStringLiteral("aimora.catalog.search")));
    QVERIFY(widget.findChild<QComboBox*>(QStringLiteral("aimora.catalog.scope")));
    QVERIFY(widget.findChild<QComboBox*>(QStringLiteral("aimora.catalog.category")));

    widget.setSearchQuery(QStringLiteral("circuit breaker"));
    QVERIFY(widget.visibleEntryIds().contains(breakerId));
    widget.setCategoryFilter(QStringLiteral("switching"));
    QVERIFY(widget.visibleEntryIds().contains(breakerId));
    widget.setScopeFilter(QStringLiteral("project"));
    QVERIFY(widget.visibleEntryIds().isEmpty());
    widget.setScopeFilter(QStringLiteral("system"));
    QVERIFY(widget.visibleEntryIds().contains(breakerId));

    QVERIFY(widget.selectEntry(breakerId));
    QCOMPARE(widget.selectedEntryId(), breakerId);
    QSignalSpy placementSpy{&widget, &CatalogLibraryWidget::placementRequested};
    auto* place = widget.findChild<QPushButton*>(QStringLiteral("aimora.catalog.place"));
    QVERIFY(place != nullptr);
    place->click();
    QCOMPARE(placementSpy.count(), 1);
    QCOMPARE(placementSpy.first().at(0).toString(), breakerId);
    QVERIFY(!placementSpy.first().at(1).toBool());
}

void CatalogTests::publishesStableDragPayload() {
    CatalogLibraryWidget widget;
    widget.setSearchQuery(QStringLiteral("circuit breaker"));
    auto* list =
        widget.findChild<QAbstractItemView*>(QStringLiteral("aimora.catalog.entries"));
    QVERIFY(list != nullptr);
    QVERIFY(list->model()->rowCount() > 0);
    const QModelIndex index = list->model()->index(0, 0);
    std::unique_ptr<QMimeData> mime{list->model()->mimeData({index})};
    QVERIFY(mime->hasFormat(QString::fromLatin1(catalogMimeType)));
    const QJsonDocument payload = QJsonDocument::fromJson(
        mime->data(QString::fromLatin1(catalogMimeType))
    );
    QCOMPARE(
        payload.object().value(QStringLiteral("catalog_id")).toString(),
        breakerId
    );
    QCOMPARE(
        payload.object().value(QStringLiteral("kind")).toString(),
        QStringLiteral("equipment")
    );
}

void CatalogTests::rejectsDuplicateEntries() {
    QJsonObject object = bundledCatalogDocument();
    QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
    entries.push_back(entries.first());
    object.insert(QStringLiteral("entries"), entries);
    QString error;
    QVERIFY(!CatalogDocument::fromJson(object, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("duplicated")));
}

QTEST_MAIN(CatalogTests)
#include "catalog_tests.moc"
