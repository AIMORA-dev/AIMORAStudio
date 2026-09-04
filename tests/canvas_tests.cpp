// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/painter_scene_renderer.hpp"
#include "aimora/studio/canvas/retained_scene.hpp"
#include "aimora/studio/canvas/scene_cache.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QtTest>

namespace {

using aimora::studio::canvas::PrimitiveKind;
using aimora::studio::canvas::PrimitiveVector;
using aimora::studio::canvas::SceneDocument;
using aimora::studio::canvas::SceneItemId;

[[nodiscard]] PrimitiveVector
makePrimitive(SceneItemId id, PrimitiveKind kind, std::initializer_list<QPointF> points) {
    PrimitiveVector primitive;
    primitive.id = id;
    primitive.kind = kind;
    primitive.points.reserve(static_cast<qsizetype>(points.size()));
    for (const QPointF& point : points) {
        primitive.points.append(point);
    }
    primitive.styleIndex = 0;
    return primitive;
}

[[nodiscard]] SceneDocument makeDocument(quint64 generation = 1) {
    using aimora::studio::canvas::ScenePageSource;
    using aimora::studio::canvas::SceneStyle;

    SceneDocument document;
    document.generation = generation;
    document.styles.append(SceneStyle{});
    document.page = ScenePageSource{9'000'000, QRectF{-100.0, -100.0, 200.0, 200.0}, Qt::white};
    return document;
}

} // namespace

class CanvasTests final : public QObject {
    Q_OBJECT

  private slots:
    void compilerRetainsEveryPrimitiveFamily();
    void symbolGeometryIsSharedAndUsesSemanticDetail();
    void spatialIndexCullsAndHitTestsDeterministically();
    void compilerRejectsInvalidAndExcessiveInput();
    void cacheEvictsLeastRecentlyUsedScenes();
    void referenceRenderingIsDeterministicAtHighDpi();
    void denseSceneCompilationAndCullingRemainBounded();
};

void CanvasTests::compilerRetainsEveryPrimitiveFamily() {
    using aimora::studio::canvas::RetainedSceneCompiler;

    SceneDocument document = makeDocument();
    document.primitives = {
        makePrimitive(1, PrimitiveKind::Line, {{-80.0, -60.0}, {-20.0, -60.0}}),
        makePrimitive(2, PrimitiveKind::Polyline, {{-80.0, -30.0}, {-50.0, -10.0}, {-20.0, -30.0}}),
        makePrimitive(3, PrimitiveKind::Arc, {{0.0, -40.0}}),
        makePrimitive(4, PrimitiveKind::Circle, {{45.0, -40.0}}),
        makePrimitive(5, PrimitiveKind::Ellipse, {{-45.0, 45.0}}),
        makePrimitive(6, PrimitiveKind::Polygon, {{20.0, 20.0}, {80.0, 20.0}, {50.0, 75.0}}),
    };
    document.primitives[2].radii = QSizeF{24.0, 18.0};
    document.primitives[2].sweepDegrees = 180.0;
    document.primitives[3].radii = QSizeF{18.0, 18.0};
    document.primitives[4].radii = QSizeF{28.0, 16.0};

    const auto result = RetainedSceneCompiler{}.compile(document);
    QVERIFY2(result.succeeded(), qPrintable(result.diagnostics.value(0).message));
    QVERIFY(result.scene->isValid());
    QCOMPARE(result.scene->statistics().sourcePrimitiveCount, qsizetype{6});
    QVERIFY(result.scene->statistics().segmentCount > 6);
    QCOMPARE(result.scene->generation(), quint64{1});
}

void CanvasTests::symbolGeometryIsSharedAndUsesSemanticDetail() {
    using aimora::studio::canvas::RetainedSceneCompiler;
    using aimora::studio::canvas::selectSymbolLevelOfDetail;
    using aimora::studio::canvas::SemanticLevelOfDetail;
    using aimora::studio::canvas::SymbolGeometrySource;
    using aimora::studio::canvas::SymbolInstanceSource;

    SceneDocument document = makeDocument();
    SymbolGeometrySource geometry;
    geometry.key = 44;
    geometry.detail = {
        makePrimitive(10, PrimitiveKind::Line, {{-8.0, 0.0}, {8.0, 0.0}}),
        makePrimitive(11, PrimitiveKind::Circle, {{0.0, 0.0}}),
    };
    geometry.detail[1].radii = QSizeF{6.0, 6.0};
    geometry.compact = {
        makePrimitive(12, PrimitiveKind::Line, {{-6.0, 0.0}, {6.0, 0.0}}),
    };
    document.symbolGeometries.append(geometry);
    document.symbolInstances = {
        SymbolInstanceSource{101, 44, QTransform::fromTranslate(-30.0, 0.0), false, false},
        SymbolInstanceSource{102, 44, QTransform::fromTranslate(30.0, 0.0), true, false},
    };

    const auto result = RetainedSceneCompiler{}.compile(document);
    QVERIFY(result.succeeded());
    QCOMPARE(result.scene->symbolGeometries().size(), qsizetype{1});
    QCOMPARE(result.scene->symbolInstances().size(), qsizetype{2});
    QCOMPARE(result.scene->symbolInstances()[0].geometryIndex, qsizetype{0});
    QCOMPARE(result.scene->symbolInstances()[1].geometryIndex, qsizetype{0});
    QCOMPARE(static_cast<int>(selectSymbolLevelOfDetail(result.scene->symbolInstances()[0], 0.1)),
             static_cast<int>(SemanticLevelOfDetail::Compact));
    QCOMPARE(static_cast<int>(selectSymbolLevelOfDetail(result.scene->symbolInstances()[0], 4.0)),
             static_cast<int>(SemanticLevelOfDetail::Detail));
}

void CanvasTests::spatialIndexCullsAndHitTestsDeterministically() {
    using aimora::studio::canvas::RetainedSceneCompiler;

    SceneDocument document = makeDocument();
    document.primitives = {
        makePrimitive(7, PrimitiveKind::Line, {{-80.0, 0.0}, {-40.0, 0.0}}),
        makePrimitive(8, PrimitiveKind::Line, {{40.0, 0.0}, {80.0, 0.0}}),
    };
    const auto result = RetainedSceneCompiler{}.compile(document);
    QVERIFY(result.succeeded());

    const auto visible = result.scene->spatialIndex().query(QRectF{-90.0, -5.0, 60.0, 10.0});
    QVERIFY(!visible.isEmpty());
    QVERIFY(visible.size() < result.scene->spatialIndex().recordCount());
    QCOMPARE(result.scene->spatialIndex().hitTest(QPointF{-60.0, 0.0}, 1.0),
             QVector<SceneItemId>{7});
    QCOMPARE(result.scene->spatialIndex().hitTest(QPointF{60.0, 0.0}, 1.0),
             QVector<SceneItemId>{8});
}

void CanvasTests::compilerRejectsInvalidAndExcessiveInput() {
    using aimora::studio::canvas::RetainedSceneCompiler;
    using aimora::studio::canvas::SceneLimits;
    using aimora::studio::canvas::SymbolGeometrySource;

    SceneDocument invalid = makeDocument();
    invalid.primitives.append(makePrimitive(1, PrimitiveKind::Line, {{0.0, 0.0}}));
    QVERIFY(!RetainedSceneCompiler{}.compile(invalid).succeeded());

    SceneDocument excessive = makeDocument();
    excessive.primitives = {
        makePrimitive(1, PrimitiveKind::Line, {{0.0, 0.0}, {1.0, 1.0}}),
        makePrimitive(2, PrimitiveKind::Line, {{2.0, 2.0}, {3.0, 3.0}}),
    };
    SceneLimits limits;
    limits.maximumSourcePrimitives = 1;
    QVERIFY(!RetainedSceneCompiler{}.compile(excessive, limits).succeeded());

    SceneDocument excessiveCompiledGeometry = makeDocument();
    SymbolGeometrySource firstGeometry;
    firstGeometry.key = 1;
    firstGeometry.detail.append(makePrimitive(11, PrimitiveKind::Line, {{0.0, 0.0}, {1.0, 1.0}}));
    SymbolGeometrySource secondGeometry;
    secondGeometry.key = 2;
    secondGeometry.detail.append(makePrimitive(12, PrimitiveKind::Line, {{2.0, 2.0}, {3.0, 3.0}}));
    excessiveCompiledGeometry.symbolGeometries = {firstGeometry, secondGeometry};
    SceneLimits compiledLimits;
    compiledLimits.maximumCompiledSegments = 1;
    QVERIFY(
        !RetainedSceneCompiler{}.compile(excessiveCompiledGeometry, compiledLimits).succeeded());
}

void CanvasTests::cacheEvictsLeastRecentlyUsedScenes() {
    using aimora::studio::canvas::RetainedSceneCache;
    using aimora::studio::canvas::RetainedSceneCompiler;
    using aimora::studio::canvas::SceneCacheLimits;

    SceneDocument firstDocument = makeDocument(1);
    firstDocument.primitives.append(
        makePrimitive(1, PrimitiveKind::Line, {{0.0, 0.0}, {1.0, 1.0}}));
    SceneDocument secondDocument = makeDocument(2);
    secondDocument.primitives.append(
        makePrimitive(2, PrimitiveKind::Line, {{2.0, 2.0}, {3.0, 3.0}}));
    const auto first = RetainedSceneCompiler{}.compile(firstDocument).scene;
    const auto second = RetainedSceneCompiler{}.compile(secondDocument).scene;
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);

    RetainedSceneCache cache{SceneCacheLimits{1, 16 * 1024 * 1024}};
    QVERIFY(cache.insert(QStringLiteral("first"), first));
    QVERIFY(cache.insert(QStringLiteral("second"), second));
    QCOMPARE(cache.entryCount(), qsizetype{1});
    QVERIFY(cache.find(QStringLiteral("first")) == nullptr);
    QVERIFY(cache.find(QStringLiteral("second")) != nullptr);
    QVERIFY(cache.estimatedBytes() <= cache.limits().maximumBytes);
}

void CanvasTests::referenceRenderingIsDeterministicAtHighDpi() {
    using aimora::studio::canvas::PainterSceneRenderer;
    using aimora::studio::canvas::RenderPalette;
    using aimora::studio::canvas::RetainedSceneCompiler;
    using aimora::studio::canvas::ViewportState;

    SceneDocument document = makeDocument();
    document.primitives.append(makePrimitive(1, PrimitiveKind::Line, {{-50.0, 0.0}, {50.0, 0.0}}));
    const auto scene = RetainedSceneCompiler{}.compile(document).scene;
    QVERIFY(scene != nullptr);

    const PainterSceneRenderer renderer;
    const QImage first = renderer.renderReferenceImage(
        *scene, ViewportState{}, QSize{640, 360}, RenderPalette{}, 2.0);
    const QImage second = renderer.renderReferenceImage(
        *scene, ViewportState{}, QSize{640, 360}, RenderPalette{}, 2.0);
    QVERIFY(!first.isNull());
    QCOMPARE(first.devicePixelRatio(), 2.0);
    QCOMPARE(first, second);
}

void CanvasTests::denseSceneCompilationAndCullingRemainBounded() {
    using aimora::studio::canvas::RetainedSceneCompiler;

    SceneDocument document = makeDocument();
    constexpr int side = 160;
    document.primitives.reserve(side * side);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const auto id = static_cast<SceneItemId>(1 + (y * side) + x);
            document.primitives.append(
                makePrimitive(id,
                              PrimitiveKind::Line,
                              {{static_cast<qreal>(x * 10), static_cast<qreal>(y * 10)},
                               {static_cast<qreal>((x * 10) + 8), static_cast<qreal>(y * 10)}}));
        }
    }

    QElapsedTimer timer;
    timer.start();
    const auto result = RetainedSceneCompiler{}.compile(document);
    const qint64 compileMilliseconds = timer.elapsed();
    QVERIFY(result.succeeded());
    QCOMPARE(result.scene->statistics().segmentCount, qsizetype{side * side});

    timer.restart();
    const auto visible = result.scene->spatialIndex().query(QRectF{400.0, 400.0, 200.0, 120.0});
    const qint64 queryMicroseconds = timer.nsecsElapsed() / 1'000;
    QVERIFY(!visible.isEmpty());
    QVERIFY(visible.size() < result.scene->statistics().spatialRecordCount / 10);
    QVERIFY(compileMilliseconds < 10'000);
    QVERIFY(queryMicroseconds < 1'000'000);
    qInfo("dense retained scene: compile=%lldms query=%lldus candidates=%lld bytes=%lld",
          compileMilliseconds,
          queryMicroseconds,
          static_cast<long long>(visible.size()),
          static_cast<long long>(result.scene->statistics().estimatedBytes));
}

QTEST_MAIN(CanvasTests)

#include "canvas_tests.moc"
