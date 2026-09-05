// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include <QWheelEvent>
#include "aimora/studio/canvas/retained_scene.hpp"
#include "aimora/studio/commands/drawing_interaction.hpp"
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/shell/drawing_scene_transport.hpp"
#include "aimora/studio/protocol/service_process.hpp"

#include <QApplication>
#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>

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
    void canonicalHistoryCommandsDispatchWithoutDrawingTargets() {
        aimora::studio::shell::DrawingWorkspace workspace;
        QVector<aimora::studio::commands::CanonicalEditRequest> requests;
        workspace.setCanonicalEditHandler([&](const auto& request) {
            requests.append(request);
            return true;
        });
        for (const QString& command : {QStringLiteral("undo"), QStringLiteral("redo")}) {
            QVERIFY(workspace.executeCommandText(command));
            const auto& request = requests.last();
            QCOMPARE(request.commandId, QStringLiteral("edit.") + command);
            QVERIFY(request.points.isEmpty());
            QVERIFY(request.selectedItemIds.isEmpty());
            QVERIFY(request.semanticIds.isEmpty());
        }
        QCOMPARE(requests.size(), 2);
        QVERIFY(workspace.executeCommandText(QStringView{}));
        QCOMPARE(requests.size(), 3);
        QCOMPARE(requests.last().commandId, QStringLiteral("edit.redo"));
        QVERIFY(workspace.executeCommandText(QStringLiteral("l")));
        QVERIFY(workspace.executeCommandText(QStringLiteral("0,0")));
        QVERIFY(!workspace.executeCommandText(QStringLiteral("edit.undo")));
        QVERIFY(!workspace.executeCommandText(QStringLiteral("edit.redo")));
        QVERIFY(workspace.executeCommandText(QStringLiteral("undo")));
        QCOMPARE(requests.size(), 3);
    }

    void quarterRotationCommandSubmitsOnePivotAndSelection() {
        using namespace aimora::studio::commands;
        DrawingCommandSession session;
        QCOMPARE(session.begin(QStringLiteral("R90")), CommandStartResult::Started);
        QVERIFY(!session.complete({41, 42}).has_value());
        QVERIFY(session.acceptCoordinateInput(QStringLiteral("1.25,-2.5"), QPointF{}));
        QVERIFY(!session.acceptPoint(QPointF{}));
        const auto request = session.complete({41, 42});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("modify.rotate_quarter"));
        QCOMPARE(request->points, (QVector<QPointF>{QPointF(1.25, -2.5)}));
        QCOMPARE(request->selectedItemIds, (QVector<quint64>{41, 42}));
    }

    void axisMirrorCommandsSubmitSelectionAndOneExactLinePoint() {
        using namespace aimora::studio::commands;
        for (const QString& alias : {QStringLiteral("mih"), QStringLiteral("miv")}) {
            DrawingCommandSession session;
            QCOMPARE(session.begin(alias), CommandStartResult::Started);
            QVERIFY(!session.complete({41}).has_value());
            QVERIFY(session.acceptCoordinateInput(QStringLiteral("0.1000000000000000000000000000000001,2"), QPointF{}));
            QVERIFY(!session.acceptPoint(QPointF(3, 4)));
            QVERIFY(session.preview().segments.isEmpty());
            const auto request = session.complete({41});
            QVERIFY(request.has_value());
            QCOMPARE(request->commandId, alias == QStringLiteral("mih") ?
                QStringLiteral("modify.mirror_horizontal") : QStringLiteral("modify.mirror_vertical"));
            QCOMPARE(request->points.size(), 1);
            QCOMPARE(request->selectedItemIds, (QVector<quint64>{41}));
            QCOMPARE(request->attributes.value(QStringLiteral("coordinate_inputs")).toArray().size(), 1);
        }
    }

    void ellipseCommandUsesTwoExactCornersAndClosedPreview() {
        using namespace aimora::studio::commands;
        DrawingCommandSession session;
        QCOMPARE(session.begin(QStringLiteral("EL")), CommandStartResult::Started);
        QVERIFY(session.acceptCoordinateInput(QStringLiteral("-4,-2"), QPointF{}));
        session.updatePointer(QPointF(4, 2));
        const auto preview = session.preview();
        QCOMPARE(preview.segments.size(), 64);
        QCOMPARE(preview.segments.first().p1(), preview.segments.last().p2());
        for (const QLineF& segment : preview.segments) {
            const QPointF point = segment.p1();
            QVERIFY(std::abs(point.x() * point.x() / 16 + point.y() * point.y() / 4 - 1) < 1e-12);
        }
        QVERIFY(!session.complete({}).has_value());
        QVERIFY(session.acceptCoordinateInput(QStringLiteral("@8,4"), QPointF(-4, -2)));
        QVERIFY(!session.acceptPoint(QPointF(8, 8)));
        const auto request = session.complete({});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("draw.ellipse"));
        QCOMPARE(request->points.size(), 2);
        QCOMPARE(request->points.last(), QPointF(4, 2));
        QCOMPARE(request->attributes.value(QStringLiteral("coordinate_inputs")).toArray().size(), 2);
    }

    void workspaceArcRejectionRetainsPointsForEndpointCorrection() {
        using namespace aimora::studio::commands;
        aimora::studio::shell::DrawingWorkspace workspace;
        QVector<CanonicalEditRequest> submissions;
        bool acceptSubmission = false;
        workspace.setCanonicalEditHandler([&](const CanonicalEditRequest& request) {
            submissions.append(request);
            return acceptSubmission;
        });

        QVERIFY(workspace.executeCommandText(QStringLiteral("A")));
        QVERIFY(workspace.executeCommandText(QStringLiteral("0,0")));
        QVERIFY(workspace.executeCommandText(QStringLiteral("1,1")));
        QVERIFY(!workspace.executeCommandText(QString{}));
        QVERIFY(submissions.isEmpty());
        QVERIFY(workspace.executeCommandText(QStringLiteral("2,0")));
        QVERIFY(!workspace.executeCommandText(QString{}));
        QCOMPARE(submissions.size(), 1);
        QCOMPARE(submissions.first().commandId, QStringLiteral("draw.arc"));
        QCOMPARE(submissions.first().points.size(), 3);

        QVERIFY(workspace.executeCommandText(QStringLiteral("U")));
        QVERIFY(workspace.executeCommandText(QStringLiteral("@2,-1")));
        acceptSubmission = true;
        QVERIFY(workspace.executeCommandText(QString{}));
        QCOMPARE(submissions.size(), 2);
        const auto& corrected = submissions.last();
        QCOMPARE(corrected.commandId, QStringLiteral("draw.arc"));
        QCOMPARE(corrected.points.size(), 3);
        QCOMPARE(corrected.points.at(0), QPointF(0.0, 0.0));
        QCOMPARE(corrected.points.at(1), QPointF(1.0, 1.0));
        QCOMPARE(corrected.points.at(2), QPointF(3.0, 0.0));
        QCOMPARE(corrected.attributes.value(QStringLiteral("coordinate_inputs")).toArray().size(), 3);
        QVERIFY(corrected.semanticIds.isEmpty());
    }

    void arcCommandPreviewsCurvatureAndAllowsEndpointCorrection() {
        using namespace aimora::studio::commands;
        DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{u"a"}), CommandStartResult::Started);
        QVERIFY(session.acceptCoordinateInput(QStringView{u"0,0"}, {}));
        QVERIFY(session.acceptCoordinateInput(QStringView{u"1,1"}, *session.anchor()));
        QVERIFY(!session.complete({}).has_value());
        session.updatePointer(QPointF{2, 0});
        const auto preview = session.preview();
        QVERIFY(preview.segments.size() > 2);
        QCOMPARE(preview.segments.first().p1(), QPointF(0, 0));
        QCOMPARE(preview.segments.last().p2(), QPointF(2, 0));
        for(const auto& segment : preview.segments) {
            QVERIFY(std::abs(std::hypot(segment.p1().x() - 1, segment.p1().y()) - 1) < 1e-12);
            QVERIFY(segment.p1().y() >= -1e-12);
        }
        QVERIFY(session.acceptCoordinateInput(QStringView{u"2,0"}, *session.anchor()));
        QVERIFY(!session.acceptPoint(QPointF{3, 0}));
        QVERIFY(session.undoPathVertex());
        QVERIFY(session.acceptCoordinateInput(QStringView{u"@1,-1"}, *session.anchor()));
        const auto request = session.complete({});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("draw.arc"));
        QCOMPARE(request->points.size(), 3);
        QCOMPARE(request->attributes.value(QStringLiteral("coordinate_inputs")).toArray().size(), 3);
    }

    void overflowingPrecisionConstraintsReturnFiniteUnsnappedInput_data() {
        QTest::addColumn<bool>("polar");
        QTest::newRow("orthogonal") << false;
        QTest::newRow("polar") << true;
    }

    void overflowingPrecisionConstraintsReturnFiniteUnsnappedInput() {
        QFETCH(bool, polar);
        using namespace aimora::studio::commands;
        SnapSettings settings;
        settings.orthoEnabled = !polar;
        settings.polarEnabled = polar;
        const QPointF input{1e308, 1e308};
        const auto result = SnapResolver{}.resolve(input, QPointF{-1e308, -1e308},
            PrecisionViewport{}, QSizeF{800, 600}, {}, settings);
        QCOMPARE(result.scenePoint, input);
        QVERIFY(std::isfinite(result.scenePoint.x()));
        QVERIFY(std::isfinite(result.scenePoint.y()));
        QCOMPARE(result.kind, SnapKind::None);
        QVERIFY(result.guides.isEmpty());
    }

    void gridSnapDoesNotDisplayUnrelatedAlignmentGuides() {
        using namespace aimora::studio::commands;
        SnapSettings settings;
        settings.gridSpacing = 10;
        settings.tolerancePixels = 2;
        const QVector<SnapCandidate> candidates{
            {1, QPointF{1, 50}, SnapKind::Endpoint, {}},
            {2, QPointF{-50, 1}, SnapKind::Endpoint, {}},
        };
        const SnapResolver resolver;
        const auto grid = resolver.resolve(QPointF{0.1, 0.1}, std::nullopt,
            PrecisionViewport{}, QSizeF{800, 600}, candidates, settings);
        QCOMPARE(grid.kind, SnapKind::Grid);
        QCOMPARE(grid.scenePoint, QPointF(0, 0));
        QVERIFY(grid.guides.isEmpty());
        settings.gridEnabled = false;
        const auto aligned = resolver.resolve(QPointF{0.1, 0.1}, std::nullopt,
            PrecisionViewport{}, QSizeF{800, 600}, candidates, settings);
        QCOMPARE(aligned.kind, SnapKind::Alignment);
        QCOMPARE(aligned.scenePoint, QPointF(1, 1));
        QCOMPARE(aligned.guides.size(), 2);
    }

    void cancelingZoomPromptPreservesTheActiveDrawing_data() {
        QTest::addColumn<bool>("keyboardEscape");
        QTest::newRow("escape-key") << true;
        QTest::newRow("cancel-command") << false;
    }

    void cancelingZoomPromptPreservesTheActiveDrawing() {
        QFETCH(bool, keyboardEscape);
        aimora::studio::shell::DrawingWorkspace workspace;
        workspace.setScene(makeScene());
        QVector<aimora::studio::commands::CanonicalEditRequest> requests;
        workspace.setCanonicalEditHandler([&](const auto& request) {
            requests.append(request);
            return true;
        });
        QVERIFY(workspace.executeCommandText(QStringView{u"l"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"1,2"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(!workspace.executeCommandText(QStringView{u"0x"}));
        if(keyboardEscape) {
            QTest::keyClick(workspace.interactionSurface(), Qt::Key_Escape);
        } else {
            QVERIFY(workspace.executeCommandText(QStringView{u"cancel"}));
        }
        QVERIFY(workspace.executeCommandText(QStringView{u"@10,0"}));
        QVERIFY(requests.isEmpty());
        QVERIFY(workspace.executeCommandText(QStringView{}));
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().points, (QVector<QPointF>{QPointF{1, 2}, QPointF{11, 2}}));
        QVERIFY(workspace.executeCommandText(QStringView{u"l"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"1,2"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QTest::keyClick(workspace.interactionSurface(), Qt::Key_Escape);
        QTest::keyClick(workspace.interactionSurface(), Qt::Key_Escape);
        QVERIFY(!workspace.executeCommandText(QStringView{u"@10,0"}));
        QCOMPARE(requests.size(), 1);
    }

    void objectZoomFitsOnlySelectionAndKeepsItSelected() {
        aimora::studio::shell::DrawingWorkspace workspace;
        workspace.resize(800, 600);
        workspace.show();
        QCoreApplication::processEvents();
        workspace.setScene(makeScene());
        const auto original = workspace.precisionViewport();
        QVERIFY(!workspace.zoomToSelection());
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom);
        QVERIFY(workspace.zoomToExtents());
        const qreal allObjectsZoom = workspace.precisionViewport().zoom;
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"p"}));
        const QPoint pixel = workspace.precisionViewport().pixelPoint(QPointF{-20, 0},
            QSizeF{workspace.interactionSurface()->size()}).toPoint();
        QTest::mouseClick(workspace.interactionSurface(), Qt::LeftButton, Qt::NoModifier, pixel);
        QVERIFY(workspace.selection().contains(41));
        QVERIFY(!workspace.selection().contains(42));
        const auto selection = workspace.selection().selectedIds();
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"o"}));
        QVERIFY(workspace.precisionViewport().zoom > allObjectsZoom);
        QCOMPARE(workspace.selection().selectedIds(), selection);
        QCOMPARE(workspace.dispatchedCanonicalEditCount(), 0);
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"p"}));
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom);
        QCOMPARE(workspace.selection().selectedIds(), selection);
    }

    void previousZoomRestoresWheelAnchorAndExtentsWithoutNoOpEntries() {
        aimora::studio::shell::DrawingWorkspace workspace;
        workspace.resize(800, 600);
        workspace.show();
        QCoreApplication::processEvents();
        QVERIFY(workspace.interactionSurface()->width() > 48);
        QVERIFY(workspace.interactionSurface()->height() > 48);
        workspace.setScene(makeScene());
        const auto original = workspace.precisionViewport();
        QVERIFY(workspace.zoomToExtents());
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"p"}));
        QCOMPARE(workspace.precisionViewport().center, original.center);
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom);

        const QPointF cursor{20, 20};
        const QSizeF extent{workspace.interactionSurface()->size()};
        const QPointF anchoredPoint = original.scenePoint(cursor, extent);
        QWheelEvent wheel{cursor, cursor, QPoint{}, QPoint{0, 120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false};
        QVERIFY(QApplication::sendEvent(workspace.interactionSurface(), &wheel));
        QVERIFY(workspace.precisionViewport().zoom > original.zoom);
        const QPointF afterWheel = workspace.precisionViewport().scenePoint(cursor, extent);
        QVERIFY(std::abs(afterWheel.x() - anchoredPoint.x()) < 1e-9);
        QVERIFY(std::abs(afterWheel.y() - anchoredPoint.y()) < 1e-9);
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"1x"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"p"}));
        QCOMPARE(workspace.precisionViewport().center, original.center);
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom);
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(!workspace.executeCommandText(QStringView{u"p"}));
        QCOMPARE(workspace.dispatchedCanonicalEditCount(), 0);
    }

    void previousZoomRestoresViewsWithBoundedLocalHistory() {
        aimora::studio::shell::DrawingWorkspace workspace;
        workspace.setScene(makeScene());
        const auto original = workspace.precisionViewport();
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"2x"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"0.5x"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"p"}));
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom * 2);
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"previous"}));
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom);
        QCOMPARE(workspace.precisionViewport().center, original.center);
        for(int index = 0; index < 40; ++index) {
            QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
            QVERIFY(workspace.executeCommandText(index % 2 == 0 ? QStringView{u"2x"} : QStringView{u"0.5x"}));
        }
        for(int index = 0; index < 32; ++index) {
            QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
            QVERIFY(workspace.executeCommandText(QStringView{u"p"}));
        }
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        QVERIFY(!workspace.executeCommandText(QStringView{u"p"}));
        QCOMPARE(workspace.dispatchedCanonicalEditCount(), 0);
    }

    void circleRadiusRejectsNoncanonicalNumbersWithoutLosingCenter() {
        using namespace aimora::studio::commands;
        DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{u"c"}), CommandStartResult::Started);
        QVERIFY(session.acceptCoordinateInput(QStringView{u"0.1,0.2"}, {}));
        for(const QString& invalid : QStringList{QStringLiteral("1,000"), QStringLiteral("1 000"),
                QStringLiteral("1_000"), QStringLiteral("0x10"), QStringLiteral("1e99999"),
                QStringLiteral("0"), QStringLiteral("-1"), QString(4097, QLatin1Char('1'))}) {
            QVERIFY(!session.acceptCircleRadius(QStringView{invalid}));
            QCOMPARE(session.preview().fixedPoints.size(), 1);
            QCOMPARE(*session.anchor(), QPointF(0.1, 0.2));
            QCOMPARE(session.completedEditCount(), 0);
        }
        QVERIFY(session.acceptCircleRadius(QStringView{u"1.0000000000000000000000000000000002"}));
        const auto request = session.complete({});
        QVERIFY(request.has_value());
        QCOMPARE(request->points.size(), 2);
        const auto inputs = request->attributes.value(QStringLiteral("coordinate_inputs")).toArray();
        QCOMPARE(inputs[1].toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("@1.0000000000000000000000000000000002,0"));
    }

    void relativeZoomCommandPreservesDrawingGestureAndRejectsInvalidFactors() {
        aimora::studio::shell::DrawingWorkspace workspace;
        workspace.setScene(makeScene());
        QVector<aimora::studio::commands::CanonicalEditRequest> requests;
        workspace.setCanonicalEditHandler([&](const auto& request) {
            requests.append(request);
            return true;
        });
        const auto original = workspace.precisionViewport();
        QVERIFY(workspace.executeCommandText(QStringView{u"l"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"0,0"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
        for(const QString& factor : QStringList{QStringLiteral("0x"), QStringLiteral("-2x"),
                QStringLiteral("nanx"), QStringLiteral("1e309x"), QStringLiteral("2xx")}) {
            QVERIFY(!workspace.executeCommandText(QStringView{factor}));
            QCOMPARE(workspace.precisionViewport().zoom, original.zoom);
            QCOMPARE(workspace.precisionViewport().center, original.center);
        }
        QVERIFY(workspace.executeCommandText(QStringView{u"2X"}));
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom * 2);
        QCOMPARE(workspace.precisionViewport().center, original.center);
        QVERIFY(requests.isEmpty());
        QVERIFY(workspace.executeCommandText(QStringView{u"@10,0"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"zoom"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"0.5x"}));
        QCOMPARE(workspace.precisionViewport().zoom, original.zoom);
        QVERIFY(requests.isEmpty());
        QVERIFY(workspace.executeCommandText(QStringView{}));
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().commandId, QStringLiteral("draw.line"));
        QCOMPARE(requests.first().points.size(), 2);
    }

    void editingClosedPathClearsCanonicalClosureIntent_data() {
        QTest::addColumn<QString>("command");
        QTest::newRow("line") << QStringLiteral("l");
        QTest::newRow("polyline") << QStringLiteral("pl");
    }

    void editingClosedPathClearsCanonicalClosureIntent() {
        QFETCH(QString, command);
        using namespace aimora::studio::commands;
        DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{command}), CommandStartResult::Started);
        QVERIFY(session.acceptPoint(QPointF{0, 0}));
        QVERIFY(session.acceptPoint(QPointF{10, 0}));
        QVERIFY(session.acceptPoint(QPointF{10, 10}));
        QVERIFY(session.closePath());
        QVERIFY(session.undoPathVertex());
        const auto reopened = session.complete({});
        QVERIFY(reopened.has_value());
        QCOMPARE(reopened->points.size(), 3);
        QVERIFY(!reopened->attributes.contains(QStringLiteral("close_path")));
        QVERIFY(reopened->points.first() != reopened->points.last());

        QCOMPARE(session.begin(QStringView{command}), CommandStartResult::Started);
        QVERIFY(session.acceptPoint(QPointF{0, 0}));
        QVERIFY(session.acceptPoint(QPointF{10, 0}));
        QVERIFY(session.acceptPoint(QPointF{10, 10}));
        QVERIFY(session.closePath());
        QVERIFY(session.acceptPoint(QPointF{20, 20}));
        const auto extended = session.complete({});
        QVERIFY(extended.has_value());
        QCOMPARE(extended->points.size(), 5);
        QVERIFY(!extended->attributes.contains(QStringLiteral("close_path")));
        QCOMPARE(extended->points.last(), QPointF(20, 20));
    }

    void visuallyCoincidentClosureStillRequestsCanonicalClosure() {
        aimora::studio::commands::DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{u"pl"}), aimora::studio::commands::CommandStartResult::Started);
        QVERIFY(session.acceptCoordinateInput(QStringView{u"0.1000000000000000000000000000000001,0"}, {}));
        QVERIFY(session.acceptCoordinateInput(QStringView{u"10,10"}, *session.anchor()));
        QVERIFY(session.acceptCoordinateInput(QStringView{u"0.1000000000000000000000000000000002,0"}, *session.anchor()));
        QVERIFY(session.closePath());
        const auto request = session.complete({});
        QVERIFY(request.has_value());
        QCOMPARE(request->points.size(), 3);
        QCOMPARE(request->points.first(), request->points.last());
        QVERIFY(request->attributes.value(QStringLiteral("close_path")).toBool());
        QCOMPARE(request->attributes.value(QStringLiteral("coordinate_inputs")).toArray().size(), 3);
    }

    void equallyCloseAlignmentGuidesIgnoreCandidateOrder() {
        using namespace aimora::studio::commands;
        SnapSettings settings;
        settings.gridEnabled = false;
        settings.tolerancePixels = 2;
        PrecisionViewport viewport;
        const QVector<SnapCandidate> candidates{
            {20, QPointF{1, 5}, SnapKind::Endpoint, {}},
            {10, QPointF{-1, -5}, SnapKind::Endpoint, {}},
            {40, QPointF{5, 1}, SnapKind::Endpoint, {}},
            {30, QPointF{-5, -1}, SnapKind::Endpoint, {}},
        };
        QVector<SnapCandidate> reversed;
        for(auto iterator = candidates.crbegin(); iterator != candidates.crend(); ++iterator) {
            reversed.append(*iterator);
        }
        const SnapResolver resolver;
        const auto forward = resolver.resolve({}, std::nullopt, viewport, QSizeF{100, 100}, candidates, settings);
        const auto backward = resolver.resolve({}, std::nullopt, viewport, QSizeF{100, 100}, reversed, settings);
        QCOMPARE(forward.kind, SnapKind::Alignment);
        QCOMPARE(forward.scenePoint, QPointF(-1, -1));
        QCOMPARE(backward.scenePoint, forward.scenePoint);
        QCOMPARE(backward.kind, forward.kind);
        QCOMPARE(forward.guides.size(), 2);
        QCOMPARE(backward.guides.size(), forward.guides.size());
        for(qsizetype index = 0; index < forward.guides.size(); ++index) {
            QCOMPARE(backward.guides[index].orientation, forward.guides[index].orientation);
            QCOMPARE(backward.guides[index].coordinate, forward.guides[index].coordinate);
        }
    }

    void overflowingNavigationPreservesViewportState() {
        aimora::studio::commands::PrecisionViewport viewport;
        viewport.center = QPointF{1e308, 0};
        const QPointF originalCenter = viewport.center;
        const qreal originalZoom = viewport.zoom;
        QVERIFY(!viewport.zoomAt(QPointF{1e308, 0}, QSizeF{100, 100}, 1));
        QCOMPARE(viewport.center, originalCenter);
        QCOMPARE(viewport.zoom, originalZoom);
        viewport.panBy(QPointF{-1e308, 0});
        QCOMPARE(viewport.center, originalCenter);
        QVERIFY(viewport.isValid(QSizeF{100, 100}));
        viewport.center = {};
        viewport.zoom = viewport.minimumZoom;
        viewport.panBy(QPointF{1e308, 0});
        QCOMPARE(viewport.center, QPointF{});
        QVERIFY(viewport.isValid(QSizeF{100, 100}));
        viewport.panBy(QPointF{1, 2});
        QCOMPARE(viewport.center, QPointF(-100, -200));
        QVERIFY(viewport.zoomAt(QPointF{50, 50}, QSizeF{100, 100}, 1));
        QVERIFY(viewport.isValid(QSizeF{100, 100}));
    }

    void workspaceLineClosureRetainsEditableInputAfterRejection_data() {
        QTest::addColumn<bool>("asynchronous");
        QTest::newRow("immediate-rejection") << false;
        QTest::newRow("service-rejection") << true;
    }

    void workspaceLineClosureRetainsEditableInputAfterRejection() {
        QFETCH(bool, asynchronous);
        aimora::studio::shell::DrawingWorkspace workspace;
        workspace.setScene(makeScene());
        workspace.setCanonicalEditConfirmationRequired(asynchronous);
        QVector<aimora::studio::commands::CanonicalEditRequest> requests;
        bool accept = false;
        workspace.setCanonicalEditHandler([&](const auto& request) {
            requests.append(request);
            return asynchronous || accept;
        });
        QVERIFY(workspace.executeCommandText(QStringView{u"l"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"0.1000000000000000000000000000000001,0.2"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"@10,0"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"@0,99"}));
        QVERIFY(requests.isEmpty());
        QCOMPARE(workspace.executeCommandText(QStringView{u"c"}), asynchronous);
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().commandId, QStringLiteral("draw.line"));
        QCOMPARE(requests.first().points.size(), 4);
        if (asynchronous) {
            workspace.resolveCanonicalEdit(false);
            // The submitted path includes its closing vertex; remove that first.
            QVERIFY(workspace.executeCommandText(QStringView{u"u"}));
        }
        QVERIFY(workspace.executeCommandText(QStringView{u"undo"}));
        QVERIFY(workspace.executeCommandText(QStringView{u"@0,5"}));
        QCOMPARE(requests.size(), 1);
        accept = true;
        QVERIFY(workspace.executeCommandText(QStringView{u"close"}));
        QCOMPARE(requests.size(), 2);
        const auto& corrected = requests.last();
        QCOMPARE(corrected.points.size(), 4);
        QCOMPARE(corrected.points.first(), corrected.points.last());
        const auto inputs = corrected.attributes.value(QStringLiteral("coordinate_inputs")).toArray();
        QCOMPARE(inputs.size(), 4);
        QCOMPARE(inputs[0].toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("0.1000000000000000000000000000000001,0.2"));
        QCOMPARE(inputs[2].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("@0,5"));
        QCOMPARE(inputs[3].toObject().value(QStringLiteral("reference")).toInt(-1), 0);
        if (asynchronous) {
            workspace.resolveCanonicalEdit(true);
        }
        QVERIFY(workspace.executeCommandText(QStringView{}));
        QCOMPARE(requests.size(), 2);
        QVERIFY(workspace.executeCommandText(QStringView{u"cancel"}));
    }

    void connectedLineUndoAndClosePreserveExactInput() {
        aimora::studio::commands::DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{u"l"}), aimora::studio::commands::CommandStartResult::Started);
        QVERIFY(!session.undoPathVertex());
        QVERIFY(!session.closePath());
        QVERIFY(session.acceptCoordinateInput(QStringView{u"0.1000000000000000000000000000000001,0.2"}, {}));
        QVERIFY(session.acceptCoordinateInput(QStringView{u"@10,0"}, *session.anchor()));
        QVERIFY(!session.closePath());
        QVERIFY(session.acceptCoordinateInput(QStringView{u"@0,99"}, *session.anchor()));
        QVERIFY(session.undoPathVertex());
        QCOMPARE(session.preview().fixedPoints.size(), 2);
        QVERIFY(session.acceptCoordinateInput(QStringView{u"@0,5"}, *session.anchor()));
        QVERIFY(session.closePath());
        QVERIFY(session.closePath());
        QCOMPARE(session.preview().fixedPoints.size(), 4);
        QCOMPARE(session.preview().segments.size(), 3);
        QCOMPARE(session.completedEditCount(), 0);
        const auto request = session.complete({});
        QVERIFY(request.has_value());
        QCOMPARE(request->points.first(), request->points.last());
        const auto inputs = request->attributes.value(QStringLiteral("coordinate_inputs")).toArray();
        QCOMPARE(inputs.size(), 4);
        QCOMPARE(inputs[2].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("@0,5"));
        QCOMPARE(inputs[3].toObject().value(QStringLiteral("reference")).toInt(-1), 0);
        QCOMPARE(session.completedEditCount(), 1);
        QCOMPARE(session.begin(QStringView{u"c"}), aimora::studio::commands::CommandStartResult::Started);
        QVERIFY(session.acceptPoint(QPointF{1, 2}));
        QVERIFY(!session.undoPathVertex());
        QVERIFY(!session.closePath());
        QCOMPARE(session.preview().fixedPoints.size(), 1);
    }

    void connectedLineInputProducesOneExactCoordinateTransaction() {
        aimora::studio::commands::DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{u"l"}), aimora::studio::commands::CommandStartResult::Started);
        QVERIFY(session.acceptCoordinateInput(QStringView{u"0.1000000000000000000000000000000001,0.2"}, {}));
        QVERIFY(session.acceptCoordinateInput(QStringView{u"@10,0"}, *session.anchor()));
        QVERIFY(session.acceptCoordinateInput(QStringView{u"@0,5"}, *session.anchor()));
        QCOMPARE(session.preview().segments.size(), 2);
        QCOMPARE(session.completedEditCount(), 0);
        const auto request = session.complete({});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("draw.line"));
        QCOMPARE(request->points.size(), 3);
        const auto inputs = request->attributes.value(QStringLiteral("coordinate_inputs")).toArray();
        QCOMPARE(inputs.size(), 3);
        QCOMPARE(inputs[0].toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("0.1000000000000000000000000000000001,0.2"));
        QCOMPARE(inputs[1].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("@10,0"));
        QCOMPARE(inputs[2].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("@0,5"));
        QCOMPARE(session.completedEditCount(), 1);
        QVERIFY(!session.isActive());
    }

    void coordinatesPreserveAbsoluteRelativeAndPolarIntent();
    void coordinatesRejectOverflowAndReduceLargeAngles();
    void wheelZoomKeepsCursorScenePointFixed();
    void zoomExtentsFitsBoundsAndPreservesActiveDrawing();
    void snapResolverAppliesModesAndDeterministicPriority();
    void selectionSupportsHitMarqueeGripsAndVirtualNodes();
    void selectAllDeduplicatesDrawingOwnersAndPreservesTextSelection();
    void marqueeIncludesLinesAndRejectsInvalidBounds();
    void commandSessionEmitsOneRequestOnlyWhenCompleted();
    void automaticLayoutCommandsRemainTransportOnly();
    void workspaceRoutesLocalInteractionAndOneCommitCallback();
    void semanticConnectionsRequireStablePortIdentities();
    void draftingSnapsDoNotBecomeElectricalTargets();
    void canonicalDrawingDisplayIsBoundedAndIdentityPreserving();
    void keyboardRepeatsCommandsAndRetainsRejectedGestures();
    void precisionFunctionKeysToggleLocalModes();
    void gridDisplayIsIndependentOfSnappingAndSurvivesThemeChanges();
    void commandLineSpaceSubmitsRepeatsAndPreservesRejectedInput();
    void polylineVertexUndoAndClosureStayLocalUntilDispatch();
    void customAliasesValidateAtomicallyAndPersist();
    void rectangleCommandPreviewsAndRendersFourEdges();
    void copyAliasesCarrySelectionWithoutSnappedPortTargets();
    void textCommandPlacesOneUnboundAnnotation();
    void circleCommandPreservesCenterAndCircumference();
    void circleRadiusEntrySubmitsAndRetainsCenterAfterRejection();
    void scaleCommandCarriesSelectionAndOnePivot();
    void scaleFactorEntryPreservesDecimalTextAndRejectedGesture();
    void CartesianInputPreservesLiteralsAndClosingReference();
    void nativeDrawingSaveTracksConfirmedRevision();
    void catalogPlacementDispatchesStableIdentityAndDisplayPoint();
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

void InteractionTests::coordinatesRejectOverflowAndReduceLargeAngles() {
    using aimora::studio::commands::CoordinateInterpreter;
    QVERIFY(!CoordinateInterpreter::parse(QStringView{u"@1e308,0"}, {1e308, 0.0}).has_value());
    QVERIFY(!CoordinateInterpreter::parse(QStringView{u"@1e308<0"}, {1e308, 0.0}).has_value());
    const auto largeAngle = CoordinateInterpreter::parse(QStringView{u"@10<1e308"}, {});
    QVERIFY(largeAngle.has_value());
    QVERIFY(std::isfinite(largeAngle->point.x()));
    QVERIFY(std::isfinite(largeAngle->point.y()));
    QVERIFY(std::abs(std::hypot(largeAngle->point.x(), largeAngle->point.y()) - 10.0) < 1e-9);
    const auto fullTurns = CoordinateInterpreter::parse(QStringView{u"@10<810"}, {});
    QVERIFY(fullTurns.has_value());
    QVERIFY(std::abs(fullTurns->point.x()) < 1e-9);
    QVERIFY(std::abs(fullTurns->point.y() - 10.0) < 1e-9);
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
    QVERIFY((QLineF{before, after}.length() < 1.0e-9));
    const QPointF centerBeforePan = viewport.center;
    viewport.panBy({20.0, -10.0});
    QCOMPARE(viewport.center,
             centerBeforePan - QPointF(20.0 / viewport.zoom, -10.0 / viewport.zoom));
}

void InteractionTests::zoomExtentsFitsBoundsAndPreservesActiveDrawing() {
    using namespace aimora::studio;
    commands::PrecisionViewport viewport;
    QVERIFY(viewport.fitBounds({-100.0, -50.0, 200.0, 100.0}, {640.0, 480.0}));
    QCOMPARE(viewport.center, QPointF(0.0, 0.0));
    QCOMPARE(viewport.zoom, qreal{2.96});
    const auto previous = viewport;
    QVERIFY(!viewport.fitBounds({0.0, 0.0, 1e308, 1e308}, {640.0, 480.0}));
    QCOMPARE(viewport.center, previous.center);
    QCOMPARE(viewport.zoom, previous.zoom);
    QVERIFY(!viewport.fitBounds({0.0, 0.0, 1.0, 1.0}, {20.0, 20.0}));
    QVERIFY(viewport.fitBounds({10.0, 20.0, 0.0, 0.0}, {640.0, 480.0}));
    QCOMPARE(viewport.center, QPointF(10.0, 20.0));
    QCOMPARE(viewport.zoom, viewport.maximumZoom);

    shell::DrawingWorkspace workspace;
    workspace.resize(640, 480);
    workspace.show();
    QCoreApplication::processEvents();
    QVERIFY(!workspace.zoomToExtents());
    workspace.setScene(makeScene());
    std::optional<commands::CanonicalEditRequest> received;
    workspace.setCanonicalEditHandler([&received](const auto& request) {
        received = request;
        return true;
    });
    QVERIFY(workspace.executeCommandText(QStringView{u"l"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"0,0"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"z"}));
    QVERIFY(!workspace.executeCommandText(QStringView{u"invalid-option"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"e"}));
    QVERIFY(workspace.precisionViewport().zoom > 1.0);
    QVERIFY(!received.has_value());
    QVERIFY(workspace.executeCommandText(QStringView{u"@10,0"}));
    QVERIFY(workspace.executeCommandText(QStringView{}));
    QVERIFY(received.has_value());
    QCOMPARE(received->points, (QVector<QPointF>{{0.0, 0.0}, {10.0, 0.0}}));
    QVERIFY(workspace.executeCommandText(QStringView{u"ze"}));
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{1});
}

void InteractionTests::snapResolverAppliesModesAndDeterministicPriority() {
    using namespace aimora::studio::commands;
    const PrecisionViewport viewport;
    const QSizeF extent{400.0, 300.0};
    SnapSettings settings;
    settings.gridEnabled = false;
    const QVector<SnapCandidate> candidates{
        {9, QPointF{3.0, 3.0}, SnapKind::Endpoint, {}},
        {4, QPointF{3.0, 3.0}, SnapKind::ElectricalPort, {}},
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
    selection.applyMarquee(
        records, QRectF{0.0, 0.0, 10.0, 10.0}, false, SelectionOperation::Replace);
    QCOMPARE(selection.selectedIds(), QVector<quint64>{11});
    const QVector<EditHandle> handles = selection.handles(records);
    QCOMPARE(handles.size(), qsizetype{8});
    QCOMPARE(static_cast<int>(handles.front().kind), static_cast<int>(EditHandleKind::Grip));
    QCOMPARE(static_cast<int>(handles.back().kind), static_cast<int>(EditHandleKind::VirtualNode));
}

void InteractionTests::selectAllDeduplicatesDrawingOwnersAndPreservesTextSelection() {
    using namespace aimora::studio;
    commands::SelectionModel selection;
    selection.replaceSelection({41, 0, 42, 41, 42});
    QCOMPARE(selection.selectedIds(), (QVector<quint64>{41, 42}));
    selection.replaceSelection({42});
    QCOMPARE(selection.selectedIds(), QVector<quint64>{42});

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings{directory.filePath(QStringLiteral("selection.ini")), QSettings::IniFormat};
    themes::ThemeSettings themeSettings{settings};
    themes::ThemeController themeController{*qApp, themeSettings};
    shell::StudioMainWindow window{themeController, settings};
    auto* workspace = window.drawingWorkspace();
    workspace->setScene(makeScene());
    QVector<quint64> inspected;
    workspace->setInspectionSelectionHandler([&inspected](const auto& ids, bool) { inspected = ids; });
    QTest::keyClick(workspace->interactionSurface(), Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(workspace->selection().selectedIds(), (QVector<quint64>{41, 42}));
    QCOMPARE(inspected, (QVector<quint64>{41, 42}));
    QVERIFY(!workspace->selection().contains(43));
    QVERIFY(workspace->executeCommandText(QStringView{u"cancel"}));
    auto* line = window.findChild<QLineEdit*>(QStringLiteral("aimora.command-line"));
    QVERIFY(line != nullptr);
    line->setText(QStringLiteral("10,20"));
    QTest::keyClick(line, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(line->selectedText(), QStringLiteral("10,20"));
    QVERIFY(workspace->selection().selectedIds().isEmpty());
    QVERIFY(workspace->executeCommandText(QStringView{u"selectall"}));
    QCOMPARE(workspace->selection().selectedIds(), (QVector<quint64>{41, 42}));
    QCOMPARE(workspace->dispatchedCanonicalEditCount(), quint64{0});
}

void InteractionTests::marqueeIncludesLinesAndRejectsInvalidBounds() {
    using namespace aimora::studio::commands;
    SelectionModel selection;
    const QVector<SelectionRecord> records{
        {21, QRectF{-5.0, 5.0, 20.0, 0.0}},
        {22, QRectF{5.0, -5.0, 0.0, 20.0}},
        {23, QRectF{10.0, 10.0, 0.0, 0.0}},
        {24, QRectF{11.0, 5.0, 0.0, 0.0}},
        {25, QRectF{2.0, 2.0, 6.0, 0.0}},
        {26, QRectF{1e308, 0.0, 1e308, 1.0}},
    };
    selection.applyMarquee(records, {10.0, 10.0, -10.0, -10.0}, true,
                           SelectionOperation::Replace);
    QCOMPARE(selection.selectedIds(), (QVector<quint64>{21, 22, 23, 25}));
    selection.applyMarquee(records, {0.0, 0.0, 10.0, 10.0}, false,
                           SelectionOperation::Replace);
    QCOMPARE(selection.selectedIds(), (QVector<quint64>{23, 25}));
    selection.applyMarquee(records, {std::numeric_limits<qreal>::quiet_NaN(), 0.0, 1.0, 1.0},
                           true, SelectionOperation::Replace);
    QCOMPARE(selection.selectedIds(), (QVector<quint64>{23, 25}));
    selection.applyMarquee(records, {0.0, 0.0, 10.0, 10.0}, true,
                           SelectionOperation::Subtract);
    QVERIFY(selection.selectedIds().isEmpty());
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

void InteractionTests::automaticLayoutCommandsRemainTransportOnly() {
    using namespace aimora::studio::commands;
    DrawingCommandSession session;

    const auto completeLayout = [&session](QStringView alias, QStringView expectedOperation) {
        QCOMPARE(static_cast<int>(session.begin(alias)),
                 static_cast<int>(CommandStartResult::Started));
        const auto request = session.complete({});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, expectedOperation.toString());
        QVERIFY(request->points.isEmpty());
        QVERIFY(request->semanticIds.isEmpty());
    };

    completeLayout(QStringView{u"layoutinitial"}, QStringView{u"layout.initial"});
    completeLayout(QStringView{u"layout"}, QStringView{u"layout.full"});
    completeLayout(QStringView{u"layoutlocal"}, QStringView{u"layout.local"});
    completeLayout(QStringView{u"layoutincremental"}, QStringView{u"layout.incremental"});
    QCOMPARE(session.completedEditCount(), quint64{4});
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
    QVERIFY(workspace.executeCommandText(QStringView{}));
    QCOMPARE(callbackCount, quint64{1});
    QVERIFY(workspace.executeCommandText(QStringView{u"cancel"}));

    QVERIFY(workspace.executeCommandText(QStringView{u"ortho"}));
    QVERIFY(workspace.orthoEnabled());
    QVERIFY(!workspace.polarEnabled());
    QVERIFY(workspace.executeCommandText(QStringView{u"polar"}));
    QVERIFY(workspace.polarEnabled());
    QVERIFY(!workspace.orthoEnabled());
}

void InteractionTests::semanticConnectionsRequireStablePortIdentities() {
    using namespace aimora::studio::commands;
    DrawingCommandSession session;
    QCOMPARE(static_cast<int>(session.begin(QStringView{u"wire"})),
             static_cast<int>(CommandStartResult::Started));
    QVERIFY(session.acceptPoint({0.0, 0.0}, QStringLiteral("port.source")));
    QVERIFY(session.acceptPoint({20.0, 0.0}));
    QVERIFY(!session.complete({}).has_value());
    session.cancel();

    QCOMPARE(static_cast<int>(session.begin(QStringView{u"electrical.connect"})),
             static_cast<int>(CommandStartResult::Started));
    QVERIFY(session.acceptPoint({0.0, 0.0}, QStringLiteral("port.source")));
    QVERIFY(session.acceptPoint({20.0, 0.0}, QStringLiteral("port.target")));
    const auto request = session.complete({});
    QVERIFY(request.has_value());
    QCOMPARE(request->semanticIds,
             (QStringList{QStringLiteral("port.source"), QStringLiteral("port.target")}));
}

void InteractionTests::draftingSnapsDoNotBecomeElectricalTargets() {
    using namespace aimora::studio::commands;
    for (const QString& command : {QStringLiteral("line"), QStringLiteral("move")}) {
        DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{command}), CommandStartResult::Started);
        QVERIFY(session.acceptPoint({0.0, 0.0}, QStringLiteral("port.source")));
        QVERIFY(session.acceptPoint({10.0, 0.0}, QStringLiteral("port.target")));
        const auto request = session.complete({41});
        QVERIFY(request.has_value());
        QVERIFY(request->semanticIds.isEmpty());
        QCOMPARE(request->selectedItemIds, QVector<quint64>{41});
    }
    DrawingCommandSession erase;
    QCOMPARE(erase.begin(QStringView{u"erase"}), CommandStartResult::Started);
    const auto request = erase.complete({41});
    QVERIFY(request.has_value());
    QCOMPARE(request->commandId, QStringLiteral("modify.erase"));
    QVERIFY(request->points.isEmpty());
}

void InteractionTests::canonicalDrawingDisplayIsBoundedAndIdentityPreserving() {
    using aimora::studio::shell::decodeDrawingScene;
    const QJsonObject line{
        {QStringLiteral("item_id"), QStringLiteral("41")},
        {QStringLiteral("owner_id"), QStringLiteral("drawing.line.main")},
        {QStringLiteral("kind"), QStringLiteral("line")},
        {QStringLiteral("points"), QJsonArray{QJsonArray{0.1, 0.2}, QJsonArray{10.0, 20.0}}},
    };
    QJsonObject payload{{QStringLiteral("items"), QJsonArray{line}},
                        {QStringLiteral("unsupported_owner_ids"), QJsonArray{}}};
    const auto decoded = decodeDrawingScene(payload, Qt::black);
    QVERIFY(decoded.scene != nullptr);
    QCOMPARE(decoded.scene->segments().size(), qsizetype{1});
    QCOMPARE(decoded.ownerIds.value(41), QStringLiteral("drawing.line.main"));
    payload.insert(QStringLiteral("items"), QJsonArray{line, line});
    QVERIFY(!decodeDrawingScene(payload, Qt::black).scene);
    QJsonObject malformed = line;
    malformed.insert(QStringLiteral("points"), QJsonArray{QJsonArray{QStringLiteral("bad"), 0}});
    payload.insert(QStringLiteral("items"), QJsonArray{malformed});
    QVERIFY(!decodeDrawingScene(payload, Qt::black).scene);
    payload.insert(QStringLiteral("items"), QJsonArray{});
    QVERIFY(decodeDrawingScene(payload, Qt::black).scene != nullptr);
}

void InteractionTests::keyboardRepeatsCommandsAndRetainsRejectedGestures() {
    using aimora::studio::shell::DrawingWorkspace;
    DrawingWorkspace workspace;
    bool accept = false;
    int attempts = 0;
    workspace.setCanonicalEditHandler([&](const auto&) {
        ++attempts;
        return accept;
    });
    QVERIFY(!workspace.executeCommandText(QStringView{}));
    QVERIFY(workspace.executeCommandText(QStringView{u"l"}));
    QVERIFY(!workspace.executeCommandText(QStringView{}));
    QCOMPARE(attempts, 0);
    QVERIFY(workspace.executeCommandText(QStringView{u"0,0"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"10,0"}));
    QVERIFY(!workspace.executeCommandText(QStringView{}));
    QCOMPARE(attempts, 1);
    accept = true;
    QVERIFY(workspace.executeCommandText(QStringView{}));
    QCOMPARE(attempts, 2);
    QWidget* surface = workspace.interactionSurface();
    QTest::keyClick(surface, Qt::Key_Space);
    QVERIFY(workspace.executeCommandText(QStringView{u"20,0"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"30,0"}));
    QTest::keyClick(surface, Qt::Key_Return);
    QCOMPARE(attempts, 3);
    QTest::keyClick(surface, Qt::Key_Return);
    QTest::keyClick(surface, Qt::Key_Escape);
    QVERIFY(workspace.executeCommandText(QStringView{u"pl"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"cancel"}));
    QString input;
    workspace.setCommandInputHandler([&input](const QString& text) { input = text; });
    QTest::keyClick(surface, Qt::Key_L);
    QCOMPARE(input.toLower(), QStringLiteral("l"));
}

void InteractionTests::precisionFunctionKeysToggleLocalModes() {
    aimora::studio::shell::DrawingWorkspace workspace;
    QWidget* surface = workspace.interactionSurface();
    workspace.setObjectSnapEnabled(true);
    QTest::keyClick(surface, Qt::Key_F3);
    QVERIFY(!workspace.objectSnapEnabled());
    QKeyEvent repeat{QEvent::KeyPress, Qt::Key_F3, Qt::NoModifier, QString{}, true};
    QCoreApplication::sendEvent(surface, &repeat);
    QVERIFY(!workspace.objectSnapEnabled());
    QTest::keyClick(surface, Qt::Key_F3);
    QVERIFY(workspace.objectSnapEnabled());
    workspace.setGridSnapEnabled(false);
    QTest::keyClick(surface, Qt::Key_F9);
    QVERIFY(workspace.gridSnapEnabled());
    QTest::keyClick(surface, Qt::Key_F9);
    QVERIFY(!workspace.gridSnapEnabled());
    QTest::keyClick(surface, Qt::Key_F8);
    QVERIFY(workspace.orthoEnabled());
    QVERIFY(!workspace.polarEnabled());
    QTest::keyClick(surface, Qt::Key_F10);
    QVERIFY(workspace.polarEnabled());
    QVERIFY(!workspace.orthoEnabled());
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{0});
}

void InteractionTests::gridDisplayIsIndependentOfSnappingAndSurvivesThemeChanges() {
    aimora::studio::shell::DrawingWorkspace workspace;
    workspace.setScene(makeScene());
    workspace.setGridSnapEnabled(true);
    QVERIFY(workspace.gridVisible());
    QCOMPARE(workspace.sceneSurface()->renderPalette().gridSpacing, qreal{10.0});
    const QImage visible = workspace.sceneSurface()->renderReferenceImage({320, 240});
    QVERIFY(!visible.isNull());
    QTest::keyClick(workspace.interactionSurface(), Qt::Key_F7);
    QVERIFY(!workspace.gridVisible());
    QVERIFY(workspace.gridSnapEnabled());
    const QImage hidden = workspace.sceneSurface()->renderReferenceImage({320, 240});
    QVERIFY(!hidden.isNull());
    QVERIFY(visible != hidden);
    workspace.setThemeTokens(aimora::studio::themes::darkThemeTokens());
    QVERIFY(!workspace.gridVisible());
    QCOMPARE(workspace.sceneSurface()->renderPalette().gridSpacing, qreal{10.0});
    QTest::keyClick(workspace.interactionSurface(), Qt::Key_F9);
    QVERIFY(!workspace.gridSnapEnabled());
    QVERIFY(!workspace.gridVisible());
    QVERIFY(workspace.executeCommandText(QStringView{u"gridview"}));
    QVERIFY(workspace.gridVisible());
    QVERIFY(!workspace.gridSnapEnabled());
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{0});
}

void InteractionTests::commandLineSpaceSubmitsRepeatsAndPreservesRejectedInput() {
    using namespace aimora::studio;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings{directory.filePath(QStringLiteral("keyboard.ini")), QSettings::IniFormat};
    themes::ThemeSettings themeSettings{settings};
    themes::ThemeController themeController{*qApp, themeSettings};
    shell::StudioMainWindow window{themeController, settings};
    auto* line = window.findChild<QLineEdit*>(QStringLiteral("aimora.command-line"));
    QVERIFY(line != nullptr);
    auto* workspace = window.drawingWorkspace();
    QVector<commands::CanonicalEditRequest> requests;
    QAction* save = window.commandAction(QStringView{u"file.save"});
    QVERIFY(save != nullptr);
    QCOMPARE(save->shortcut(), QKeySequence{QKeySequence::Save});
    QVERIFY(!save->isEnabled());
    QVERIFY(!window.saveDrawingProject());
    workspace->setCanonicalEditHandler([&requests](const auto& request) {
        requests.append(request);
        return true;
    });
    const auto submit = [line](const QString& text) {
        line->setText(text);
        QTest::keyClick(line, Qt::Key_Space);
    };
    submit(QStringLiteral("L"));
    QVERIFY(line->text().isEmpty());
    submit(QStringLiteral("0,0"));
    submit(QStringLiteral("@10,5"));
    QVERIFY(requests.isEmpty());
    submit(QString{});
    QCOMPARE(requests.size(), qsizetype{1});
    QCOMPARE(requests.front().commandId, QStringLiteral("draw.line"));
    QCOMPARE(requests.front().points, (QVector<QPointF>{{0.0, 0.0}, {10.0, 5.0}}));
    submit(QString{});
    submit(QStringLiteral("bad-coordinate"));
    QCOMPARE(line->text(), QStringLiteral("bad-coordinate"));
    QVERIFY(line->property("aimoraInputRejected").toBool());
    submit(QStringLiteral("20,0"));
    QVERIFY(!line->property("aimoraInputRejected").toBool());
    submit(QStringLiteral("30,0"));
    submit(QString{});
    QCOMPARE(requests.size(), qsizetype{2});

    QAction* snap = window.commandAction(QStringView{u"view.object-snap"});
    QAction* grid = window.commandAction(QStringView{u"view.grid-snap"});
    QVERIFY(snap != nullptr);
    QVERIFY(grid != nullptr);
    QCOMPARE(snap->shortcut(), QKeySequence{QStringLiteral("F3")});
    QCOMPARE(grid->shortcut(), QKeySequence{QStringLiteral("F9")});
    const bool previousSnap = workspace->objectSnapEnabled();
    submit(QStringLiteral("SNAP"));
    QCOMPARE(workspace->objectSnapEnabled(), !previousSnap);
    QCOMPARE(snap->isChecked(), workspace->objectSnapEnabled());
    snap->trigger();
    QCOMPARE(workspace->objectSnapEnabled(), previousSnap);
    QCOMPARE(requests.size(), qsizetype{2});
}

void InteractionTests::polylineVertexUndoAndClosureStayLocalUntilDispatch() {
    using namespace aimora::studio;
    commands::DrawingCommandSession session;
    QVERIFY(!session.undoPolylineVertex());
    QVERIFY(!session.closePolyline());
    QCOMPARE(session.begin(QStringView{u"pl"}), commands::CommandStartResult::Started);
    QVERIFY(session.acceptPoint({0.0, 0.0}, QStringLiteral("port.origin")));
    QVERIFY(session.undoPolylineVertex());
    QVERIFY(!session.anchor().has_value());
    QVERIFY(session.preview().segments.isEmpty());
    QVERIFY(!session.undoPolylineVertex());
    QVERIFY(!session.closePolyline());
    session.cancel();
    QCOMPARE(session.begin(QStringView{u"l"}), commands::CommandStartResult::Started);
    QVERIFY(session.acceptPoint({0.0, 0.0}));
    QVERIFY(!session.undoPolylineVertex());
    QVERIFY(!session.closePolyline());

    shell::DrawingWorkspace workspace;
    QVector<commands::CanonicalEditRequest> requests;
    bool accepted = false;
    workspace.setCanonicalEditHandler([&](const auto& request) {
        requests.append(request);
        return accepted;
    });
    QVERIFY(workspace.executeCommandText(QStringView{u"pl"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"0,0"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"10,0"}));
    QVERIFY(!workspace.executeCommandText(QStringView{u"c"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"10,20"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"u"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"@0,10"}));
    QVERIFY(requests.isEmpty());
    QVERIFY(!workspace.executeCommandText(QStringView{u"close"}));
    QCOMPARE(requests.size(), qsizetype{1});
    QCOMPARE(requests.front().points,
             (QVector<QPointF>{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 0.0}}));
    QVERIFY(requests.front().semanticIds.isEmpty());
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{0});
    QVERIFY(workspace.executeCommandText(QStringView{u"undo"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"@0,15"}));
    accepted = true;
    QVERIFY(workspace.executeCommandText(QStringView{u"c"}));
    QCOMPARE(requests.size(), qsizetype{2});
    QCOMPARE(requests.last().points,
             (QVector<QPointF>{{0.0, 0.0}, {10.0, 0.0}, {10.0, 15.0}, {0.0, 0.0}}));
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{1});
    QVERIFY(workspace.executeCommandText(QStringView{}));
    QVERIFY(!workspace.executeCommandText(QStringView{u"u"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"cancel"}));
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{1});
}

void InteractionTests::customAliasesValidateAtomicallyAndPersist() {
    using namespace aimora::studio::commands;
    DrawingCommandSession session;
    const QHash<QString, QString> custom{{QStringLiteral("DL"), QStringLiteral("draw.line")}};
    QVERIFY(session.setCustomAliases(custom));
    QCOMPARE(session.commandAliases().value(QStringLiteral("dl")), QStringLiteral("draw.line"));
    QVERIFY(!session.setCustomAliases({{QStringLiteral("grid"), QStringLiteral("draw.line")}}));
    QVERIFY(!session.setCustomAliases({{QStringLiteral("xy"), QStringLiteral("draw.missing")}}));
    QVERIFY(!session.setCustomAliases({{QStringLiteral("a"), QStringLiteral("draw.line")},
                                      {QStringLiteral("A"), QStringLiteral("modify.move")}}));
    QCOMPARE(session.begin(QStringView{u"dl"}), CommandStartResult::Started);
    QVERIFY(!session.setCustomAliases({}));
    session.cancel();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("preferences.ini"));
    {
        QSettings settings{path, QSettings::IniFormat};
        aimora::studio::shell::WorkspaceSettings preferences{settings};
        QVERIFY(preferences.saveCustomCommandAliases(custom));
    }
    QSettings reopened{path, QSettings::IniFormat};
    aimora::studio::shell::WorkspaceSettings preferences{reopened};
    QCOMPARE(preferences.customCommandAliases(), custom);
    QVERIFY(session.setCustomAliases(preferences.customCommandAliases()));
    QCOMPARE(session.begin(QStringView{u"DL"}), CommandStartResult::Started);
    session.cancel();
    QVERIFY(session.setCustomAliases({}));
    QCOMPARE(session.begin(QStringView{u"dl"}), CommandStartResult::UnknownCommand);
    QCOMPARE(session.begin(QStringView{u"l"}), CommandStartResult::Started);
}

void InteractionTests::rectangleCommandPreviewsAndRendersFourEdges() {
    using namespace aimora::studio::commands;
    DrawingCommandSession session;
    QCOMPARE(session.begin(QStringView{u"rec"}), CommandStartResult::Started);
    QVERIFY(session.acceptPoint({4.0, 3.0}));
    session.updatePointer({1.0, 1.0});
    const auto preview = session.preview();
    QCOMPARE(preview.segments.size(), qsizetype{4});
    QCOMPARE(preview.segments.front().p1(), QPointF(1.0, 1.0));
    QVERIFY(!session.complete({}).has_value());
    QVERIFY(session.acceptPoint({1.0, 1.0}));
    QVERIFY(!session.acceptPoint({5.0, 5.0}));
    const auto request = session.complete({});
    QVERIFY(request.has_value());
    QCOMPARE(request->commandId, QStringLiteral("draw.rectangle"));
    const QJsonObject item{
        {QStringLiteral("item_id"), QStringLiteral("51")},
        {QStringLiteral("owner_id"), QStringLiteral("drawing.rectangle.main")},
        {QStringLiteral("kind"), QStringLiteral("polygon")},
        {QStringLiteral("points"), QJsonArray{QJsonArray{1, 1}, QJsonArray{4, 1},
                                              QJsonArray{4, 3}, QJsonArray{1, 3}}},
    };
    const auto decoded = aimora::studio::shell::decodeDrawingScene(
        {{QStringLiteral("items"), QJsonArray{item}},
         {QStringLiteral("unsupported_owner_ids"), QJsonArray{}}}, Qt::black);
    QVERIFY(decoded.scene != nullptr);
    QCOMPARE(decoded.scene->segments().size(), qsizetype{4});
}

void InteractionTests::copyAliasesCarrySelectionWithoutSnappedPortTargets() {
    using namespace aimora::studio::commands;
    for (const QString& alias : {QStringLiteral("co"), QStringLiteral("cp"), QStringLiteral("copy")}) {
        DrawingCommandSession session;
        QCOMPARE(session.begin(QStringView{alias}), CommandStartResult::Started);
        QVERIFY(session.acceptPoint({0.0, 0.0}, QStringLiteral("port.source")));
        QVERIFY(!session.complete({41}).has_value());
        QVERIFY(session.acceptPoint({1.0, 1.0}, QStringLiteral("port.target")));
        const auto request = session.complete({41});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("modify.copy"));
        QCOMPARE(request->selectedItemIds, QVector<quint64>{41});
        QVERIFY(request->semanticIds.isEmpty());
        QCOMPARE(request->points.size(), qsizetype{2});
    }
}

void InteractionTests::textCommandPlacesOneUnboundAnnotation() {
    using namespace aimora::studio::commands;
    DrawingCommandSession session;
    QCOMPARE(session.begin(QStringView{u"t"}), CommandStartResult::Started);
    QVERIFY(!session.complete({}).has_value());
    QVERIFY(session.acceptPoint({3.0, 4.0}, QStringLiteral("port.source")));
    QVERIFY(!session.acceptPoint({5.0, 6.0}));
    session.updatePointer({9.0, 9.0});
    QVERIFY(session.preview().segments.isEmpty());
    const auto request = session.complete({});
    QVERIFY(request.has_value());
    QCOMPARE(request->commandId, QStringLiteral("draw.text"));
    QVERIFY(request->semanticIds.isEmpty());
    const QJsonObject item{
        {QStringLiteral("item_id"), QStringLiteral("61")},
        {QStringLiteral("owner_id"), QStringLiteral("drawing.label.note")},
        {QStringLiteral("kind"), QStringLiteral("text")},
        {QStringLiteral("points"), QJsonArray{QJsonValue{QJsonArray{3.0, 4.0}}}},
        {QStringLiteral("text"), QStringLiteral("Feeder A \u03a9")},
    };
    const auto decoded = aimora::studio::shell::decodeDrawingScene(
        {{QStringLiteral("items"), QJsonArray{item}},
         {QStringLiteral("unsupported_owner_ids"), QJsonArray{}}}, Qt::black);
    QVERIFY(decoded.scene != nullptr);
    QCOMPARE(decoded.scene->texts().size(), qsizetype{1});
    QCOMPARE(decoded.scene->texts().first().text, QStringLiteral("Feeder A \u03a9"));
}

void InteractionTests::circleCommandPreservesCenterAndCircumference() {
    using namespace aimora::studio;
    commands::DrawingCommandSession session;
    QCOMPARE(session.begin(QStringView{u"c"}), commands::CommandStartResult::Started);
    QVERIFY(session.acceptPoint({2.0, 3.0}, QStringLiteral("port.center")));
    QVERIFY(!session.complete({}).has_value());
    session.updatePointer({5.0, 7.0});
    const auto preview = session.preview();
    QCOMPARE(preview.segments.size(), qsizetype{64});
    QCOMPARE(preview.segments.first().p1(), preview.segments.last().p2());
    for (const auto& segment : preview.segments) {
        QVERIFY(std::abs(QLineF{QPointF{2.0, 3.0}, segment.p1()}.length() - 5.0) < 1e-9);
    }
    QVERIFY(session.acceptPoint({5.0, 7.0}, QStringLiteral("port.edge")));
    QVERIFY(!session.acceptPoint({10.0, 10.0}));
    const auto request = session.complete({});
    QVERIFY(request.has_value());
    QCOMPARE(request->commandId, QStringLiteral("draw.circle"));
    QVERIFY(request->semanticIds.isEmpty());
    QJsonObject item{
        {QStringLiteral("item_id"), QStringLiteral("71")},
        {QStringLiteral("owner_id"), QStringLiteral("drawing.circle.outline")},
        {QStringLiteral("kind"), QStringLiteral("circle")},
        {QStringLiteral("points"), QJsonArray{QJsonArray{2.0, 3.0}, QJsonArray{5.0, 7.0}}},
    };
    const auto decode = [&item]() {
        return shell::decodeDrawingScene(
            {{QStringLiteral("items"), QJsonArray{item}},
             {QStringLiteral("unsupported_owner_ids"), QJsonArray{}}}, Qt::black);
    };
    const auto decoded = decode();
    QVERIFY(decoded.scene != nullptr);
    QVERIFY(decoded.scene->segments().size() > 3);
    for (const QPointF& point : decoded.scene->segments().starts) {
        QVERIFY(std::abs(QLineF{QPointF{2.0, 3.0}, point}.length() - 5.0) < 1e-9);
    }
    item.insert(QStringLiteral("points"), QJsonArray{QJsonArray{2.0, 3.0}, QJsonArray{2.0, 3.0}});
    QVERIFY(decode().scene == nullptr);
}

void InteractionTests::circleRadiusEntrySubmitsAndRetainsCenterAfterRejection() {
    using namespace aimora::studio;
    commands::DrawingCommandSession session;
    QVERIFY(!session.acceptCircleRadius(QStringView{u"5"}));
    QCOMPARE(session.begin(QStringView{u"c"}), commands::CommandStartResult::Started);
    QVERIFY(!session.acceptCircleRadius(QStringView{u"5"}));
    QVERIFY(session.acceptPoint({2.0, 3.0}));
    for (const QString& value : {QStringLiteral("0"), QStringLiteral("-1"),
                                 QStringLiteral("nan"), QStringLiteral("1e999"),
                                 QStringLiteral("5,6")}) {
        QVERIFY(!session.acceptCircleRadius(QStringView{value}));
    }
    QVERIFY(session.acceptCircleRadius(QStringView{u"5e0"}));
    const auto sized = session.complete({});
    QVERIFY(sized.has_value());
    QCOMPARE(sized->points, (QVector<QPointF>{{2.0, 3.0}, {7.0, 3.0}}));

    shell::DrawingWorkspace workspace;
    bool accepted = false;
    QVector<commands::CanonicalEditRequest> requests;
    workspace.setCanonicalEditHandler([&](const auto& request) {
        requests.append(request);
        return accepted;
    });
    QVERIFY(workspace.executeCommandText(QStringView{u"c"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"2,3"}));
    QVERIFY(!workspace.executeCommandText(QStringView{u"-5"}));
    QVERIFY(requests.isEmpty());
    QVERIFY(!workspace.executeCommandText(QStringView{u"5"}));
    QCOMPARE(requests.size(), qsizetype{1});
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{0});
    accepted = true;
    QVERIFY(workspace.executeCommandText(QStringView{u"10"}));
    QCOMPARE(requests.size(), qsizetype{2});
    QCOMPARE(requests.last().points, (QVector<QPointF>{{2.0, 3.0}, {12.0, 3.0}}));
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{1});
}

void InteractionTests::scaleCommandCarriesSelectionAndOnePivot() {
    using namespace aimora::studio::commands;
    DrawingCommandSession session;
    QCOMPARE(session.begin(QStringView{u"sc"}), CommandStartResult::Started);
    QVERIFY(!session.complete({71}).has_value());
    QVERIFY(session.acceptPoint({2.1, 3.2}, QStringLiteral("port.pivot")));
    QVERIFY(!session.acceptPoint({5.0, 6.0}));
    session.updatePointer({9.0, 10.0});
    QVERIFY(session.preview().segments.isEmpty());
    const auto request = session.complete({71});
    QVERIFY(request.has_value());
    QCOMPARE(request->commandId, QStringLiteral("modify.scale"));
    QCOMPARE(request->points.size(), qsizetype{1});
    QCOMPARE(request->selectedItemIds, QVector<quint64>{71});
    QVERIFY(request->semanticIds.isEmpty());
}

void InteractionTests::scaleFactorEntryPreservesDecimalTextAndRejectedGesture() {
    using namespace aimora::studio;
    for (const auto& spelling : {qMakePair(QStringLiteral("+.5"), QStringLiteral("0.5")),
                                qMakePair(QStringLiteral("002."), QStringLiteral("2")),
                                qMakePair(QStringLiteral("+0001.0000000000000000000000000000000001e+0"),
                                          QStringLiteral("1.0000000000000000000000000000000001e+0"))}) {
        commands::DrawingCommandSession normalized;
        QCOMPARE(normalized.begin(QStringView{u"sc"}), commands::CommandStartResult::Started);
        QVERIFY(normalized.acceptPoint({0.0, 0.0}));
        QVERIFY(normalized.acceptScaleFactor(QStringView{spelling.first}));
        const auto edit = normalized.complete({71});
        QVERIFY(edit.has_value());
        QCOMPARE(edit->attributes.value(QStringLiteral("factor")).toString(), spelling.second);
    }
    commands::DrawingCommandSession session;
    QVERIFY(!session.acceptScaleFactor(QStringView{u"2"}));
    QCOMPARE(session.begin(QStringView{u"sc"}), commands::CommandStartResult::Started);
    QVERIFY(!session.acceptScaleFactor(QStringView{u"2"}));
    QVERIFY(session.acceptPoint({0.0, 0.0}));
    for (const QString& invalid : {QStringLiteral("0"), QStringLiteral("0.00e9"),
                                   QStringLiteral("-2"), QStringLiteral("nan"),
                                   QStringLiteral("1,5"), QStringLiteral("1e")}) {
        QVERIFY(!session.acceptScaleFactor(QStringView{invalid}));
    }
    const QString exact = QStringLiteral("1.0000000000000000000000000000000001");
    QVERIFY(session.acceptScaleFactor(QStringView{exact}));
    const auto request = session.complete({71});
    QVERIFY(request.has_value());
    QCOMPARE(request->attributes.value(QStringLiteral("factor")).toString(), exact);
    QCOMPARE(session.begin(QStringView{u"sc"}), commands::CommandStartResult::Started);
    QVERIFY(session.acceptPoint({0.0, 0.0}));
    const auto next = session.complete({71});
    QVERIFY(next.has_value());
    QVERIFY(!next->attributes.contains(QStringLiteral("factor")));

    shell::DrawingWorkspace workspace;
    bool accepted = false;
    QVector<commands::CanonicalEditRequest> requests;
    workspace.setCanonicalEditHandler([&](const auto& edit) {
        requests.append(edit);
        return accepted;
    });
    QVERIFY(workspace.executeCommandText(QStringView{u"sc"}));
    QVERIFY(workspace.executeCommandText(QStringView{u"2,3"}));
    QVERIFY(!workspace.executeCommandText(QStringView{u"0"}));
    QVERIFY(requests.isEmpty());
    QVERIFY(!workspace.executeCommandText(QStringView{exact}));
    QCOMPARE(requests.size(), qsizetype{1});
    QCOMPARE(requests.first().attributes.value(QStringLiteral("factor")).toString(), exact);
    accepted = true;
    QVERIFY(workspace.executeCommandText(QStringView{u"2.5"}));
    QCOMPARE(requests.last().points, (QVector<QPointF>{{2.0, 3.0}}));
    QCOMPARE(requests.last().attributes.value(QStringLiteral("factor")).toString(), QStringLiteral("2.5"));
    QCOMPARE(workspace.dispatchedCanonicalEditCount(), quint64{1});
}

void InteractionTests::CartesianInputPreservesLiteralsAndClosingReference() {
    using namespace aimora::studio::commands;
    for (const QString& alias : {QStringLiteral("j"), QStringLiteral("join")}) {
        DrawingCommandSession join;
        QCOMPARE(join.begin(QStringView{alias}), CommandStartResult::Started);
        QVERIFY(!join.acceptPoint({1.0, 2.0}));
        const auto request = join.complete({31, 32});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("modify.join_lines"));
        QCOMPARE(request->selectedItemIds, (QVector<quint64>{31, 32}));
        QVERIFY(request->points.isEmpty());
    }
    for (const QString& alias : {QStringLiteral("x"), QStringLiteral("explode")}) {
        DrawingCommandSession explode;
        QCOMPARE(explode.begin(QStringView{alias}), CommandStartResult::Started);
        QVERIFY(!explode.acceptPoint({1.0, 2.0}));
        const auto request = explode.complete({21, 22});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("modify.explode_paths"));
        QCOMPARE(request->selectedItemIds, (QVector<quint64>{21, 22}));
        QVERIFY(request->points.isEmpty());
    }
    for (const QString& alias : {QStringLiteral("ed"), QStringLiteral("ddedit")}) {
        DrawingCommandSession textEdit;
        QCOMPARE(textEdit.begin(QStringView{alias}), CommandStartResult::Started);
        QVERIFY(!textEdit.acceptPoint({1.0, 2.0}));
        const auto request = textEdit.complete({11, 12});
        QVERIFY(request.has_value());
        QCOMPARE(request->commandId, QStringLiteral("modify.text"));
        QCOMPARE(request->selectedItemIds, (QVector<quint64>{11, 12}));
        QVERIFY(request->points.isEmpty());
        QVERIFY(request->semanticIds.isEmpty());
    }
    for (const QString& alias : {QStringLiteral("ax"), QStringLiteral("ay"), QStringLiteral("dx"), QStringLiteral("dy")}) {
        DrawingCommandSession arrangement;
        QCOMPARE(arrangement.begin(QStringView{alias}), CommandStartResult::Started);
        const bool alignment = alias.startsWith(QLatin1Char('a'));
        if (alignment) {
            QVERIFY(!arrangement.complete({1, 2, 3}).has_value());
            QVERIFY(arrangement.acceptCoordinateInput(QStringView{u"1.0000000000000000000000000000000001,2"}, {}));
        }
        QVERIFY(!arrangement.acceptPoint({4.0, 5.0}));
        const auto edit = arrangement.complete({1, 2, 3});
        QVERIFY(edit.has_value());
        QCOMPARE(edit->selectedItemIds, (QVector<quint64>{1, 2, 3}));
        QVERIFY(edit->semanticIds.isEmpty());
        QCOMPARE(edit->points.size(), alignment ? qsizetype{1} : qsizetype{0});
    }
    {
        DrawingCommandSession normalized;
        QCOMPARE(normalized.begin(QStringView{u"l"}), CommandStartResult::Started);
        QVERIFY(normalized.acceptCoordinateInput(QStringView{u"+.5,-0002."}, {}));
        QVERIFY(normalized.acceptCoordinateInput(QStringView{u"@000.1000000000000000000000000000000001,+.25"},
                                                  *normalized.anchor()));
        const auto edit = normalized.complete({});
        QVERIFY(edit.has_value());
        const auto exactInputs = edit->attributes.value(QStringLiteral("coordinate_inputs")).toArray();
        QCOMPARE(exactInputs[0].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("0.5,-2"));
        QCOMPARE(exactInputs[1].toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("@0.1000000000000000000000000000000001,0.25"));
    }
    for (const auto& spelling : {qMakePair(QStringLiteral("+.5"), QStringLiteral("@0.5,0")),
                                qMakePair(QStringLiteral("002."), QStringLiteral("@2,0"))}) {
        DrawingCommandSession circle;
        QCOMPARE(circle.begin(QStringView{u"c"}), CommandStartResult::Started);
        QVERIFY(circle.acceptPoint({0.0, 0.0}));
        QVERIFY(!circle.acceptCircleRadius(QStringView{u"-1"}));
        QVERIFY(circle.acceptCircleRadius(QStringView{spelling.first}));
        const auto edit = circle.complete({});
        QVERIFY(edit.has_value());
        const auto exactInputs = edit->attributes.value(QStringLiteral("coordinate_inputs")).toArray();
        QCOMPARE(exactInputs[1].toObject().value(QStringLiteral("text")).toString(), spelling.second);
    }
    DrawingCommandSession session;
    QCOMPARE(session.begin(QStringView{u"pl"}), CommandStartResult::Started);
    const QString literal = QStringLiteral("0.1000000000000000000000000000000001,0.2");
    QVERIFY(session.acceptCoordinateInput(QStringView{literal}, {}));
    QVERIFY(session.acceptCoordinateInput(QStringView{u"@1,0"}, *session.anchor()));
    QVERIFY(session.acceptCoordinateInput(QStringView{u"@0,2"}, *session.anchor()));
    QVERIFY(session.undoPolylineVertex());
    QVERIFY(session.acceptCoordinateInput(QStringView{u"@0,3"}, *session.anchor()));
    QVERIFY(session.closePolyline());
    const auto request = session.complete({});
    QVERIFY(request.has_value());
    const auto inputs = request->attributes.value(QStringLiteral("coordinate_inputs")).toArray();
    QCOMPARE(inputs.size(), request->points.size());
    QCOMPARE(inputs[0].toObject().value(QStringLiteral("text")).toString(), literal);
    QCOMPARE(inputs[2].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("@0,3"));
    QCOMPARE(inputs[3].toObject().value(QStringLiteral("reference")).toInt(-1), 0);
}

void InteractionTests::nativeDrawingSaveTracksConfirmedRevision() {
    using namespace aimora::studio;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mockService = qEnvironmentVariable("AIMORA_MOCK_SERVICE_PATH");
    QVERIFY(!mockService.isEmpty());
    protocol::ServiceLaunchConfiguration configuration{
        .program = mockService,
        .programArguments = {},
        .allowedRoots = {directory.path()},
        .workerProgram = {},
        .workerArguments = {},
        .limits = {},
        .startupTimeoutMs = 5000,
        .shutdownTimeoutMs = 2000,
        .maximumAutomaticRestarts = 0,
    };
    protocol::ServiceProcess process{configuration};
    process.start();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), protocol::ServiceProcess::State::Ready, 5000);
    QVERIFY(process.client() != nullptr);
    QSettings settings{directory.filePath(QStringLiteral("save.ini")), QSettings::IniFormat};
    themes::ThemeSettings themeSettings{settings};
    themes::ThemeController themeController{*qApp, themeSettings};
    shell::StudioMainWindow window{themeController, settings};
    window.bindProjectService(process.client());
    QVERIFY(window.openDrawingProject(directory.filePath(QStringLiteral("drawing.aimora.yaml"))));
    QTRY_VERIFY_WITH_TIMEOUT(!window.semanticRevision().isEmpty(), 3000);
    QVERIFY(!window.isWindowModified());
    QAction* save = window.commandAction(QStringView{u"file.save"});
    QVERIFY(save != nullptr);
    QVERIFY(save->isEnabled());
    const QString originalRevision = window.semanticRevision();
    auto* workspace = window.drawingWorkspace();
    QVERIFY(workspace->executeCommandText(QStringView{u"l"}));
    QVERIFY(workspace->executeCommandText(QStringView{u"0,0"}));
    QVERIFY(workspace->executeCommandText(QStringView{u"0,0"}));
    QVERIFY(workspace->executeCommandText(QStringView{}));
    QVERIFY(!workspace->executeCommandText(QStringView{u"l"}));
    QTRY_COMPARE_WITH_TIMEOUT(process.client()->pendingRequestCount(), qsizetype{0}, 3000);
    QCOMPARE(window.semanticRevision(), originalRevision);
    QVERIFY(!window.isWindowModified());
    auto* commandLine = window.findChild<QLineEdit*>(QStringLiteral("aimora.command-line"));
    QVERIFY(commandLine != nullptr);
    QVERIFY(commandLine->property("aimoraInputRejected").toBool());
    QVERIFY(!workspace->executeCommandText(QStringView{u"l"}));
    QVERIFY(workspace->executeCommandText(QStringView{u"cancel"}));
    QVERIFY(workspace->executeCommandText(QStringView{u"l"}));
    QVERIFY(workspace->executeCommandText(QStringView{u"0,0"}));
    QVERIFY(workspace->executeCommandText(QStringView{u"10,0"}));
    QVERIFY(workspace->executeCommandText(QStringView{}));
    QVERIFY(!window.saveDrawingProject());
    QTRY_VERIFY_WITH_TIMEOUT(window.isWindowModified(), 3000);
    QVERIFY(window.semanticRevision() != originalRevision);
    QCOMPARE(workspace->sceneSurface()->scene()->segments().size(), qsizetype{1});
    const QString editedRevision = window.semanticRevision();
    save->trigger();
    QVERIFY(!window.saveDrawingProject());
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWindowModified(), 3000);
    QCOMPARE(window.semanticRevision(), editedRevision);
    QVERIFY(window.saveDrawingProject());
    QTRY_COMPARE_WITH_TIMEOUT(process.client()->pendingRequestCount(), qsizetype{0}, 3000);
    QVERIFY(!window.isWindowModified());
    process.stop();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), protocol::ServiceProcess::State::Stopped, 5000);
    QVERIFY(!save->isEnabled());
    QVERIFY(!window.commandAction(QStringView{u"draw.line"})->isEnabled());
    QVERIFY(!window.saveDrawingProject());
}

void InteractionTests::catalogPlacementDispatchesStableIdentityAndDisplayPoint() {
    using aimora::studio::shell::DrawingWorkspace;
    DrawingWorkspace workspace;
    std::optional<aimora::studio::commands::CanonicalEditRequest> received;
    workspace.setCanonicalEditHandler([&received](const auto& request) {
        received = request;
        return true;
    });
    const QString catalogId = QStringLiteral("aimora://catalog/system/assembly.feeder_bay@1.0.0");
    QVERIFY(workspace.requestEquipmentPlacement(catalogId, true, QPointF{25.0, 40.0}));
    QVERIFY(received.has_value());
    QCOMPARE(received->commandId, QStringLiteral("equipment.place"));
    QCOMPARE(received->semanticIds, QStringList{catalogId});
    QCOMPARE(received->points, (QVector<QPointF>{QPointF{25.0, 40.0}}));
    QCOMPARE(received->attributes.value(QStringLiteral("catalog_id")).toString(), catalogId);
    QVERIFY(received->attributes.value(QStringLiteral("assembly")).toBool());
}

QTEST_MAIN(InteractionTests)

#include "interaction_tests.moc"
