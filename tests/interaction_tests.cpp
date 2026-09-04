// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/retained_scene.hpp"
#include "aimora/studio/commands/drawing_interaction.hpp"
#include "aimora/studio/shell/studio_shell.hpp"

#include <QApplication>
#include <QtTest>

#include <cmath>

namespace {

[[nodiscard]] std::shared_ptr<const aimora::studio::canvas::RetainedScene> makeScene() {
    using namespace aimora::studio::canvas;
    PrimitiveVector horizontal;
    horizontal.id = 41;
    horizontal.kind = PrimitiveKind::Line;
    horizontal.points = {{-40.0, 0.0}, {40.0, 0.0}};

    PrimitiveVector vertical;
    vertical.id = 42;
    vertical.kind = PrimitiveKind::Line;
    vertical.points = {{0.0, -40.0}, {0.0, 40.0}};

    SceneDocument document;
    document.generation = 90;
    document.styles.append(SceneStyle{});
    document.primitives = {horizontal, vertical};
    document.page = ScenePageSource{43, QRectF{-100.0, -80.0, 200.0, 160.0}, Qt::white};
    return RetainedSceneCompiler{}.compile(document).scene;
}

} // namespace

class InteractionTests final : public QObject {
    Q_OBJECT

  private slots:
    void coordinatesPreserveAbsoluteRelativeAndPolarIntent();
    void wheelZoomKeepsCursorScenePointFixed();
    void snapResolverAppliesModesAndDeterministicPriority();
    void selectionSupportsHitMarqueeGripsAndVirtualNodes();
    void commandSessionEmitsOneRequestOnlyWhenCompleted();
    void workspaceRoutesLocalInteractionAndOneCommitCallback();
};

void InteractionTests::coordinatesPreserveAbsoluteRelativeAndPolarIntent() {
    using aimora::studio::commands::CoordinateInputKind;
    using aimora::studio::commands::CoordinateInterpreter;

    const auto absolute = CoordinateInterpreter::parse(QStringView{u"12.5,-3"}, {1.0, 2.0});
    QVERIFY(absolute.has_value());
    QCOMPARE(static_cast<int>(absolute->kind), static_cast<int>(CoordinateInputKind::Absolute));
    QCOMPARE(absolute->point, QPointF(12.5, -3.0));

    const auto relative = CoordinateInterpreter::parse(QStringView{u"@2,4"}, {10.0, -2.0});
    QVERIFY(relative.has_value());
    QCOMPARE(static_cast<int>(relative->kind), static_cast<int>(CoordinateInputKind::Relative));
    QCOMPARE(relative->point, QPointF(12.0, 2.0));

    const auto polar = CoordinateInterpreter::parse(QStringView{u"@10<90"}, {2.0, 3.0});
    QVERIFY(polar.has_value());
    QCOMPARE(static_cast<int>(polar->kind), static_cast<int>(CoordinateInputKind::Polar));
    QVERIFY(std::abs(polar->point.x() - 2.0) < 1.0e-9);
    QVERIFY(std::abs(polar->point.y() - 13.0) < 1.0e-9);
    QVERIFY(!CoordinateInterpreter::parse(QStringView{u"@bad"}, {}).has_value());
}

void InteractionTests::wheelZoomKeepsCursorScenePointFixed() {
    using aimora::studio::commands::PrecisionViewport;
    PrecisionViewport viewport;
    viewport.center = {25.0, -10.0};
    const QSizeF extent{800.0, 600.0};
    const QPointF cursor{123.0, 456.0};
    const QPointF before = viewport.scenePoint(cursor, extent);
    QVERIFY(viewport.zoomAt(cursor, extent, 3.0));
    const QPointF after = viewport.scenePoint(cursor, extent);
    QVERIFY(QLineF{before, after}.length() < 1.0e-9);
    const QPointF centerBeforePan = viewport.center;
    viewport.panBy({20.0, -10.0});
    QCOMPARE(viewport.center,
             centerBeforePan - QPointF(20.0 / viewport.zoom, -10.0 / viewport.zoom));
}

void InteractionTests::snapResolverAppliesModesAndDeterministicPriority() {
    using namespace aimora::studio::commands;
    const PrecisionViewport viewport;
    const QSizeF extent{400.0, 300.0};
    SnapSettings settings;
    settings.gridEnabled = false;
    const QVector<SnapCandidate> candidates{
        {9, QPointF{3.0, 3.0}, SnapKind::Endpoint},
        {4, QPointF{3.0, 3.0}, SnapKind::ElectricalPort},
    };
    const SnapResult port =
        SnapResolver{}.resolve({3.5, 3.0}, std::nullopt, viewport, extent, candidates, settings);
    QCOMPARE(static_cast<int>(port.kind), static_cast<int>(SnapKind::ElectricalPort));
    QCOMPARE(port.itemId, quint64{4});

    settings.objectEnabled = false;
    settings.electricalPortEnabled = false;
    settings.alignmentEnabled = false;
    settings.orthoEnabled = true;
    const SnapResult ortho =
        SnapResolver{}.resolve({8.0, 2.0}, QPointF{0.0, 0.0}, viewport, extent, {}, settings);
    QCOMPARE(ortho.scenePoint, QPointF(8.0, 0.0));

    settings.orthoEnabled = false;
    settings.polarEnabled = true;
    settings.polarIncrementDegrees = 45.0;
    const SnapResult polar =
        SnapResolver{}.resolve({9.0, 8.0}, QPointF{0.0, 0.0}, viewport, extent, {}, settings);
    QVERIFY(std::abs(polar.scenePoint.x() - polar.scenePoint.y()) < 1.0e-9);
}

void InteractionTests::selectionSupportsHitMarqueeGripsAndVirtualNodes() {
    using namespace aimora::studio::commands;
    SelectionModel selection;
    selection.applyHit({9, 4}, SelectionOperation::Replace);
    QCOMPARE(selection.selectedIds(), QVector<quint64>{4});
    selection.applyHit({9}, SelectionOperation::Add);
    QCOMPARE(selection.selectedIds(), (QVector<quint64>{4, 9}));
    selection.applyHit({4}, SelectionOperation::Toggle);
    QCOMPARE(selection.selectedIds(), QVector<quint64>{9});

    const QVector<SelectionRecord> records{
        {11, QRectF{0.0, 0.0, 5.0, 5.0}},
        {12, QRectF{8.0, 8.0, 5.0, 5.0}},
    };
    selection.applyMarquee(records,
                           QRectF{0.0, 0.0, 10.0, 10.0},
                           false,
                           SelectionOperation::Replace);
    QCOMPARE(selection.selectedIds(), QVector<quint64>{11});
    const QVector<EditHandle> handles = selection.handles(records);
    QCOMPARE(handles.size(), qsizetype{8});
    QCOMPARE(static_cast<int>(handles.front().kind), static_cast<int>(EditHandleKind::Grip));
    QCOMPARE(static_cast<int>(handles.back().kind),
             static_cast<int>(EditHandleKind::VirtualNode));
}

void InteractionTests::commandSessionEmitsOneRequestOnlyWhenCompleted() {
    using namespace aimora::studio::commands;
    DrawingCommandSession session;
    QCOMPARE(static_cast<int>(session.begin(QStringView{u"l"})),
             static_cast<int>(CommandStartResult::Started));
    QVERIFY(session.acceptPoint({0.0, 0.0}));
    session.updatePointer({10.0, 0.0});
    QCOMPARE(session.preview().segments.size(), qsizetype{1});
    QVERIFY(!session.complete({}).has_value());
    QVERIFY(session.acceptPoint({10.0, 0.0}));
    const auto request = session.complete({41});
    QVERIFY(request.has_value());
    QCOMPARE(request->serial, quint64{1});
    QCOMPARE(request->commandId, QStringLiteral("draw.line"));
    QCOMPARE(request->points.size(), qsizetype{2});
    QCOMPARE(request->selectedItemIds, QVector<quint64>{41});
    QCOMPARE(session.completedEditCount(), quint64{1});
    QVERIFY(!session.complete({}).has_value());
    QCOMPARE(session.completedEditCount(), quint64{1});
}

void InteractionTests::workspaceRoutesLocalInteractionAndOneCommitCallback() {
    using aimora::studio::shell::DrawingWorkspace;
    DrawingWorkspace workspace;
    workspace.resize(640, 480);
    workspace.setScene(makeScene());
    quint64 callbackCount = 0;
    workspace.setCanonicalEditHandler([&callbackCount](const auto& request) {
        ++callbackCount;
        return request.commandId == QStringLiteral("draw.line");
    });
    workspace.show();
    QCoreApplication::processEvents();

    QWidget* interaction = workspace.interactionSurface();
    QVERIFY(interaction != nullptr);
    QVERIFY(interaction->hasMouseTracking());
    QVERIFY(workspace.executeCommandText(QStringView{u"line"}));
    QTest::mouseClick(interaction, Qt::LeftButton, Qt::NoModifier, QPoint{250, 240});
    QTest::mouseClick(interaction, Qt::LeftButton, Qt::NoModifier, QPoint{390, 240});
    QVERIFY(workspace.executeCommandText(QStringView{}));
    QCOMPARE(callbackCount, quint64{1});
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{1});
    QVERIFY(!workspace.executeCommandText(QStringView{}));
    QCOMPARE(callbackCount, quint64{1});

    QVERIFY(workspace.executeCommandText(QStringView{u"ortho"}));
    QVERIFY(workspace.orthoEnabled());
    QVERIFY(!workspace.polarEnabled());
    QVERIFY(workspace.executeCommandText(QStringView{u"polar"}));
    QVERIFY(workspace.polarEnabled());
    QVERIFY(!workspace.orthoEnabled());
}

QTEST_MAIN(InteractionTests)

#include "interaction_tests.moc"
