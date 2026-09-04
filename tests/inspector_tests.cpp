// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/inspector/inspection_document.hpp"
#include "aimora/studio/inspector/schema_inspector_widget.hpp"

#include <QCheckBox>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QTableView>
#include <QtTest>

using namespace aimora::studio::inspector;

namespace {

[[nodiscard]] QJsonObject
field(const QString& path, const QString& label, const QString& kind, const QString& unit = {}) {
    QJsonObject result{
        {QStringLiteral("path"), path},
        {QStringLiteral("label"), label},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("canonical_unit"), unit},
        {QStringLiteral("display_units"), QJsonArray{}},
        {QStringLiteral("choices"), QJsonArray{}},
        {QStringLiteral("dependencies"), QJsonArray{}},
        {QStringLiteral("read_only"), false},
    };
    if (!unit.isEmpty()) {
        result.insert(QStringLiteral("display_units"),
                      QJsonArray{QJsonObject{{QStringLiteral("symbol"), unit}},
                                 QJsonObject{{QStringLiteral("symbol"), QStringLiteral("kV")}}});
    }
    if (kind == QStringLiteral("choice")) {
        result.insert(QStringLiteral("choices"),
                      QJsonArray{QJsonObject{{QStringLiteral("value"), QStringLiteral("closed")},
                                             {QStringLiteral("label"), QStringLiteral("Closed")}},
                                 QJsonObject{{QStringLiteral("value"), QStringLiteral("open")},
                                             {QStringLiteral("label"), QStringLiteral("Open")}}});
    }
    return result;
}

[[nodiscard]] QJsonObject value(QJsonValue item, const QString& unit = {}, bool mixed = false) {
    return {
        {QStringLiteral("value"), std::move(item)},
        {QStringLiteral("canonical_unit"), unit},
        {QStringLiteral("provenance"), QStringLiteral("catalog.breaker")},
        {QStringLiteral("issues"), QJsonArray{}},
        {QStringLiteral("mixed"), mixed},
    };
}

[[nodiscard]] QJsonObject inspectionFixture() {
    const QJsonArray fields{
        field(QStringLiteral("general.enabled"),
              QStringLiteral("Enabled"),
              QStringLiteral("boolean")),
        field(QStringLiteral("general.poles"), QStringLiteral("Poles"), QStringLiteral("integer")),
        field(QStringLiteral("ratings.voltage"),
              QStringLiteral("Rated voltage"),
              QStringLiteral("number"),
              QStringLiteral("V")),
        field(QStringLiteral("general.name"), QStringLiteral("Name"), QStringLiteral("text")),
        field(QStringLiteral("general.state"), QStringLiteral("State"), QStringLiteral("choice")),
        field(QStringLiteral("general.bus"), QStringLiteral("Bus"), QStringLiteral("reference")),
        field(QStringLiteral("curves.points"), QStringLiteral("Curve"), QStringLiteral("curve")),
        field(QStringLiteral("ratings.rows"), QStringLiteral("Ratings"), QStringLiteral("table")),
    };
    const QJsonArray rows{
        QJsonObject{{QStringLiteral("row_id"), QStringLiteral("row.1")},
                    {QStringLiteral("x"), 1.0},
                    {QStringLiteral("y"), 2.0}},
    };
    return {
        {QStringLiteral("schema_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("revision"), QStringLiteral("7")},
        {QStringLiteral("identity"),
         QJsonObject{{QStringLiteral("project_id"), QStringLiteral("project.1")},
                     {QStringLiteral("asset_id"), QStringLiteral("asset.breaker.1")},
                     {QStringLiteral("projection_id"), QStringLiteral("projection.breaker.1")},
                     {QStringLiteral("view_id"), QStringLiteral("view.sld.1")},
                     {QStringLiteral("equipment_class"), QStringLiteral("breaker")},
                     {QStringLiteral("result_bindings"),
                      QJsonArray{QStringLiteral("result.short-circuit.1")}}}},
        {QStringLiteral("sections"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("general")},
                                {QStringLiteral("title"), QStringLiteral("General and ratings")},
                                {QStringLiteral("available"), true},
                                {QStringLiteral("fields"), fields}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("study_facets")},
                                {QStringLiteral("title"), QStringLiteral("Study facets")},
                                {QStringLiteral("available"), false},
                                {QStringLiteral("unavailable_reason"),
                                 QStringLiteral("No active study supports this equipment.")},
                                {QStringLiteral("fields"), QJsonArray{}}}}},
        {QStringLiteral("values"),
         QJsonObject{{QStringLiteral("general.enabled"), value(true)},
                     {QStringLiteral("general.poles"), value(3)},
                     {QStringLiteral("ratings.voltage"), value(11000.0, QStringLiteral("V"))},
                     {QStringLiteral("general.name"), value(QStringLiteral("Q1"))},
                     {QStringLiteral("general.state"), value(QStringLiteral("closed"))},
                     {QStringLiteral("general.bus"), value(QStringLiteral("bus.1"))},
                     {QStringLiteral("curves.points"), value(rows)},
                     {QStringLiteral("ratings.rows"), value(rows)}}},
        {QStringLiteral("undo_available"), true},
        {QStringLiteral("redo_available"), false},
    };
}

} // namespace

class InspectorTests final : public QObject {
    Q_OBJECT

  private slots:
    void parsesSchemaAndBuildsDeterministicTransaction();
    void rendersNativeEditorsAndStagesLocally();
    void rejectsUnstableTableRows();
};

void InspectorTests::parsesSchemaAndBuildsDeterministicTransaction() {
    QString error;
    const auto document = InspectionDocument::fromJson(inspectionFixture(), &error);
    QVERIFY2(document.has_value(), qPrintable(error));
    QCOMPARE(document->sections.size(), 2);
    QCOMPARE(document->values.size(), 8);
    QCOMPARE(document->identity.assetIds, QStringList{QStringLiteral("asset.breaker.1")});

    InspectionDraft draft;
    draft.reset(*document);
    draft.setEdit(QStringLiteral("ratings.voltage"), 13.8, QStringLiteral("kV"));
    draft.setEdit(QStringLiteral("general.name"), QStringLiteral("Q2"));
    QVERIFY(draft.isDirty());
    const QJsonObject request = draft.commitRequest();
    QCOMPARE(request.value(QStringLiteral("base_revision")).toString(), QStringLiteral("7"));
    const QJsonArray edits = request.value(QStringLiteral("edits")).toArray();
    QCOMPARE(edits.size(), 2);
    QCOMPARE(edits.at(0).toObject().value(QStringLiteral("path")).toString(),
             QStringLiteral("general.name"));
    QCOMPARE(edits.at(1).toObject().value(QStringLiteral("display_unit")).toString(),
             QStringLiteral("kV"));
}

void InspectorTests::rendersNativeEditorsAndStagesLocally() {
    SchemaInspectorWidget widget;
    QVERIFY(widget.setDocument(inspectionFixture()));
    QCOMPARE(widget.findChildren<QGroupBox*>().size(), 2);
    QCOMPARE(widget.findChildren<QTableView*>().size(), 2);
    QVERIFY(widget.findChild<QCheckBox*>(QStringLiteral("aimora.inspector.field.general.enabled")));
    auto* voltage =
        widget.findChild<QLineEdit*>(QStringLiteral("aimora.inspector.field.ratings.voltage"));
    QVERIFY(voltage != nullptr);
    QCOMPARE(voltage->accessibleName(), QStringLiteral("Rated voltage"));
    voltage->setText(QStringLiteral("13.8"));
    QVERIFY(QMetaObject::invokeMethod(voltage, "editingFinished", Qt::DirectConnection));
    QVERIFY(widget.hasLocalEdits());
    QCOMPARE(widget.pendingCommitRequest()
                 .value(QStringLiteral("edits"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("path"))
                 .toString(),
             QStringLiteral("ratings.voltage"));
}

void InspectorTests::rejectsUnstableTableRows() {
    QJsonObject fixture = inspectionFixture();
    QJsonObject values = fixture.value(QStringLiteral("values")).toObject();
    values.insert(QStringLiteral("ratings.rows"),
                  value(QJsonArray{QJsonObject{{QStringLiteral("x"), 1.0}}}));
    fixture.insert(QStringLiteral("values"), values);
    QString error;
    QVERIFY(!InspectionDocument::fromJson(fixture, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("row_id")));
}

QTEST_MAIN(InspectorTests)
#include "inspector_tests.moc"
