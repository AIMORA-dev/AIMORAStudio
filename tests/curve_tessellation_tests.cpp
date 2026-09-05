// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/retained_scene.hpp"

#include <QTest>
#include <QElapsedTimer>
#include <cmath>
#include <limits>

using namespace aimora::studio::canvas;

class CurveTessellationTests final : public QObject {
    Q_OBJECT

  private slots:
    void spatialLeafBoundariesPreserveEveryRecord_data() {
        QTest::addColumn<int>("recordCount");
        for (const int count : {0, 1, 7, 8, 9, 15, 16, 17, 63, 64, 65, 127, 128, 129, 1000}) {
            QTest::newRow(qPrintable(QString::number(count))) << count;
        }
    }

    void spatialLeafBoundariesPreserveEveryRecord() {
        QFETCH(int, recordCount);
        QVector<SpatialRecord> records;
        for (int position = 0; position < recordCount; ++position) {
            records.append(SpatialRecord{
                QRectF{static_cast<qreal>(position * 3), 0, 1, 1},
                static_cast<SceneItemId>(position + 1), SceneItemKind::Segment,
                static_cast<qsizetype>(position)});
        }
        const SceneSpatialIndex index{records};
        QVERIFY(index.isValid());
        QCOMPARE(index.recordCount(), recordCount);
        const auto matches = index.query(QRectF{-1, -1, recordCount * 3.0 + 2, 3});
        QCOMPARE(matches.size(), recordCount);
        for (int position = 0; position < recordCount; ++position) {
            QCOMPARE(matches[position].id, static_cast<SceneItemId>(position + 1));
            const auto hits = index.hitTest(QPointF{position * 3.0 + 0.5, 0.5}, 0);
            QCOMPARE(hits.size(), 1);
            QCOMPARE(hits.front(), static_cast<SceneItemId>(position + 1));
        }
        if (recordCount == 0) {
            QCOMPARE(index.nodeCount(), 0);
        } else {
            qsizetype leafSlots = 1;
            while ((recordCount + leafSlots - 1) / leafSlots > 8) {
                leafSlots *= 2;
            }
            QVERIFY(index.nodeCount() <= leafSlots * 2 - 1);
        }
    }

    void spatialNodeReservationTracksLeafCapacity() {
        QVector<SpatialRecord> records;
        records.reserve(1000);
        for(SceneItemId id = 1; id <= 1000; ++id) {
            records.append(SpatialRecord{
                QRectF{static_cast<qreal>(id % 50), static_cast<qreal>(id / 50), 1, 1},
                id, SceneItemKind::Segment, static_cast<qsizetype>(id - 1)});
        }
        const SceneSpatialIndex index{records};
        QVERIFY(index.isValid());
        QCOMPARE(index.recordCount(), 1000);
        QCOMPARE(index.query(QRectF{-1, -1, 100, 100}).size(), 1000);
        const qsizetype nodeBytes = static_cast<qsizetype>(sizeof(QRectF) + 4 * sizeof(qsizetype));
        const qsizetype previousReservation = records.capacity() * static_cast<qsizetype>(sizeof(SpatialRecord))
            + (2 * records.size() - 1) * nodeBytes;
        QVERIFY(index.estimatedBytes() >= index.recordCount() * static_cast<qsizetype>(sizeof(SpatialRecord))
            + index.nodeCount() * nodeBytes);
        QVERIFY(index.estimatedBytes() < previousReservation / 2);
        qInfo() << "1000-record index capacity bytes:" << index.estimatedBytes()
                << "previous reservation lower bound:" << previousReservation;
    }

    void spatialIndexRejectsOverflowingCombinedExtent() {
        const QVector<SpatialRecord> records{
            {QRectF{-1e308, 0, 1e307, 1}, 1, SceneItemKind::Segment, 0},
            {QRectF{9e307, 0, 1e307, 1}, 2, SceneItemKind::Segment, 1}};
        const SceneSpatialIndex index{records};
        QVERIFY(!index.isValid());
        QCOMPARE(index.nodeCount(), 0);
        QVERIFY(index.query(QRectF{0, 0, 1, 1}).isEmpty());

        SceneDocument document;
        document.styles.append(SceneStyle{});
        document.primitives.append(PrimitiveVector{
            .id = 1, .kind = PrimitiveKind::Line,
            .points = {QPointF{-1e308, 0}, QPointF{-9e307, 0}}});
        document.primitives.append(PrimitiveVector{
            .id = 2, .kind = PrimitiveKind::Line,
            .points = {QPointF{9e307, 0}, QPointF{1e308, 0}}});
        const auto result = RetainedSceneCompiler{}.compile(document);
        QVERIFY(!result.succeeded());
        QVERIFY(result.scene == nullptr);
        QCOMPARE(result.diagnostics.first().code, QStringLiteral("scene.spatial_bounds"));
    }

    void spatialQueriesRejectNonfiniteAreasAndPoints() {
        const SceneSpatialIndex index{QVector<SpatialRecord>{
            {QRectF{0, 0, 10, 10}, 1, SceneItemKind::Segment, 0}}};
        QVERIFY(index.isValid());
        const qreal infinity = std::numeric_limits<qreal>::infinity();
        QVERIFY(index.query(QRectF{0, 0, infinity, 10}).isEmpty());
        QVERIFY(index.hitTest(QPointF{infinity, 1}, 1).isEmpty());
        QVERIFY(index.hitTest(QPointF{1, 1}, std::numeric_limits<qreal>::max()).isEmpty());
        QCOMPARE(index.hitTest(QPointF{1, 1}, 1), QVector<SceneItemId>{1});
        const SceneSpatialIndex invalid{QVector<SpatialRecord>{
            {QRectF{0, 0, infinity, 10}, 1, SceneItemKind::Segment, 0}}};
        QVERIFY(!invalid.isValid());
    }

    void oversizedPolylineIsRejectedBeforeExpansion() {
        SceneDocument document;
        document.styles.append(SceneStyle{});
        PrimitiveVector polyline;
        polyline.id = 1;
        polyline.kind = PrimitiveKind::Polyline;
        polyline.points.reserve(100001);
        for(int index = 0; index <= 100000; ++index) {
            polyline.points.append(QPointF{static_cast<qreal>(index), 0});
        }
        document.primitives.append(polyline);
        SceneLimits limits;
        limits.maximumCompiledSegments = 99999;
        QElapsedTimer timer;
        timer.start();
        const auto result = RetainedSceneCompiler{}.compile(document, limits);
        const qint64 elapsed = timer.nsecsElapsed();
        QVERIFY(!result.succeeded());
        QVERIFY(result.scene == nullptr);
        QCOMPARE(result.diagnostics.size(), 1);
        QCOMPARE(result.diagnostics.first().code, QStringLiteral("scene.segment_limit"));
        qInfo() << "oversized 100000-segment polyline rejected in ns:" << elapsed;
    }

    void closingPolygonEdgeCountsAgainstRemainingBudget() {
        SceneDocument document;
        document.styles.append(SceneStyle{});
        document.primitives.append(PrimitiveVector{
            .id = 1, .kind = PrimitiveKind::Line,
            .points = {QPointF{10, 10}, QPointF{20, 20}}});
        document.primitives.append(PrimitiveVector{
            .id = 2, .kind = PrimitiveKind::Polygon,
            .points = {QPointF{0, 0}, QPointF{1, 0}, QPointF{1, 1}}});
        SceneLimits limits;
        limits.maximumCompiledSegments = 3;
        const auto rejected = RetainedSceneCompiler{}.compile(document, limits);
        QVERIFY(!rejected.succeeded());
        QCOMPARE(rejected.diagnostics.first().code, QStringLiteral("scene.segment_limit"));
        QCOMPARE(rejected.diagnostics.first().itemId, SceneItemId{2});
        limits.maximumCompiledSegments = 4;
        const auto accepted = RetainedSceneCompiler{}.compile(document, limits);
        QVERIFY(accepted.succeeded());
        QCOMPARE(accepted.scene->statistics().segmentCount, 4);
    }

    void finiteEndpointsWithOverflowingExtentAreRejected() {
        SceneDocument document;
        document.styles.append(SceneStyle{});
        document.primitives.append(PrimitiveVector{
            .id = 1, .kind = PrimitiveKind::Line,
            .points = {QPointF{-1e308, 0}, QPointF{1e308, 0}}});
        const auto result = RetainedSceneCompiler{}.compile(document);
        QVERIFY(!result.succeeded());
        QVERIFY(result.scene == nullptr);
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.diagnostics.first().code, QStringLiteral("scene.nonfinite_bounds"));
        QCOMPARE(result.diagnostics.first().itemId, SceneItemId{1});
    }

    void externallyConstructedSegmentBuffersRejectInfiniteBounds() {
        SegmentBuffer segments;
        segments.starts.append(QPointF{0, 0});
        segments.ends.append(QPointF{1, 1});
        segments.styleIndices.append(0);
        segments.ids.append(1);
        segments.bounds.append(QRectF{0, 0, std::numeric_limits<qreal>::infinity(), 1});
        QVERIFY(!segments.isValid(1));
        segments.bounds[0] = QRectF{0, 0, 1, 1};
        QVERIFY(segments.isValid(1));
    }

    void circlesAndEllipsesMatchEquivalentFullArcs() {
        for(const int detail : {8, 32, 128, 256}) {
            SceneLimits limits;
            limits.arcSegmentsPerCircle = detail;
            SceneDocument document;
            document.styles.append(SceneStyle{});
            document.primitives.append(PrimitiveVector{
                .id = 1, .kind = PrimitiveKind::Circle, .points = {QPointF{7.25, -3.5}},
                .radii = QSizeF{17, 17}});
            document.primitives.append(PrimitiveVector{
                .id = 2, .kind = PrimitiveKind::Ellipse, .points = {QPointF{-19.5, 14}},
                .radii = QSizeF{200, 75}});
            const auto shared = RetainedSceneCompiler{}.compile(document, limits);
            QVERIFY(shared.succeeded());
            for(auto& primitive : document.primitives) {
                primitive.kind = PrimitiveKind::Arc;
                primitive.startDegrees = 0.0;
                primitive.sweepDegrees = 360.0;
            }
            const auto direct = RetainedSceneCompiler{}.compile(document, limits);
            QVERIFY(direct.succeeded());
            QCOMPARE(shared.scene->segments().starts, direct.scene->segments().starts);
            QCOMPARE(shared.scene->segments().ends, direct.scene->segments().ends);
            QCOMPARE(shared.scene->segments().bounds, direct.scene->segments().bounds);
            QCOMPARE(shared.scene->statistics().estimatedBytes, direct.scene->statistics().estimatedBytes);
        }
    }

    void largeFiniteArcAnglesRemainPeriodic() {
        for(const qreal start : {1e308, -1e308}) {
            SceneDocument document;
            document.styles.append(SceneStyle{});
            document.primitives.append(PrimitiveVector{
                .id = 1, .kind = PrimitiveKind::Arc, .points = {QPointF{0, 0}},
                .radii = QSizeF{10, 5}, .startDegrees = start, .sweepDegrees = 90.0});
            const auto large = RetainedSceneCompiler{}.compile(document);
            QVERIFY(large.succeeded());
            document.primitives[0].startDegrees = std::remainder(start, 360.0);
            const auto normalized = RetainedSceneCompiler{}.compile(document);
            QVERIFY(normalized.succeeded());
            QCOMPARE(large.scene->segments().starts, normalized.scene->segments().starts);
            QCOMPARE(large.scene->segments().ends, normalized.scene->segments().ends);
        }
    }

    void denseCurveDetailHasMeasuredBoundedCost() {
        SceneDocument document;
        document.styles.append(SceneStyle{});
        for(SceneItemId id = 1; id <= 2000; ++id) {
            document.primitives.append(PrimitiveVector{
                .id = id, .kind = PrimitiveKind::Circle,
                .points = {QPointF{static_cast<qreal>(id % 50) * 30.0,
                                  static_cast<qreal>(id / 50) * 30.0}},
                .radii = QSizeF{10, 10}});
        }
        for(const int detail : {32, SceneLimits{}.arcSegmentsPerCircle}) {
            SceneLimits limits;
            limits.arcSegmentsPerCircle = detail;
            QElapsedTimer timer;
            timer.start();
            const auto result = RetainedSceneCompiler{}.compile(document, limits);
            const qint64 elapsed = timer.nsecsElapsed();
            QVERIFY(result.succeeded());
            QCOMPARE(result.scene->statistics().segmentCount, qsizetype{2000} * detail);
            QVERIFY(result.scene->statistics().estimatedBytes <= limits.maximumBytes);
            qInfo().noquote() << QStringLiteral("2000 circles: detail=%1 segments=%2 bytes=%3 compile_ms=%4")
                .arg(detail).arg(result.scene->statistics().segmentCount)
                .arg(result.scene->statistics().estimatedBytes).arg(static_cast<double>(elapsed) / 1e6);
        }
    }

    void defaultCircleHasSubpixelChordErrorAtThousandPixelRadius() {
        SceneDocument document;
        document.styles.append(SceneStyle{});
        document.primitives.append(PrimitiveVector{
            .id = 1, .kind = PrimitiveKind::Circle, .points = {QPointF{0, 0}},
            .radii = QSizeF{1000, 1000}});
        const auto result = RetainedSceneCompiler{}.compile(document);
        QVERIFY(result.succeeded());
        const auto& segments = result.scene->segments();
        QCOMPARE(segments.size(), SceneLimits{}.arcSegmentsPerCircle);
        QCOMPARE(segments.ends.last(), segments.starts.first());
        for(qsizetype index = 0; index < segments.size(); ++index) {
            const QPointF midpoint = (segments.starts.at(index) + segments.ends.at(index)) / 2.0;
            const qreal error = 1000.0 - std::hypot(midpoint.x(), midpoint.y());
            QVERIFY(error >= -1e-9);
            QVERIFY(error < 0.31);
            QCOMPARE(segments.ids.at(index), SceneItemId{1});
        }
    }

    void fullEllipsesAndSignedArcsCloseExactly() {
        for(const auto kind : {PrimitiveKind::Ellipse, PrimitiveKind::Arc}) {
            for(const qreal sweep : {360.0, -360.0}) {
                SceneDocument document;
                document.styles.append(SceneStyle{});
                document.primitives.append(PrimitiveVector{
                    .id = 1, .kind = kind, .points = {QPointF{7.25, -3.5}},
                    .radii = QSizeF{100, 35}, .startDegrees = 17.0, .sweepDegrees = sweep});
                const auto result = RetainedSceneCompiler{}.compile(document);
                QVERIFY(result.succeeded());
                const auto& segments = result.scene->segments();
                QCOMPARE(segments.ends.last().x(), segments.starts.first().x());
                QCOMPARE(segments.ends.last().y(), segments.starts.first().y());
            }
        }
    }

    void improvedDetailStillHonorsTheTotalSegmentBudget() {
        SceneDocument document;
        document.styles.append(SceneStyle{});
        document.primitives.append(PrimitiveVector{
            .id = 1, .kind = PrimitiveKind::Circle, .points = {QPointF{0, 0}},
            .radii = QSizeF{10, 10}});
        SceneLimits limits;
        limits.maximumCompiledSegments = limits.arcSegmentsPerCircle - 1;
        const auto result = RetainedSceneCompiler{}.compile(document, limits);
        QVERIFY(!result.succeeded());
        QVERIFY(result.scene == nullptr);
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.diagnostics.first().code, QStringLiteral("scene.segment_limit"));
    }
};

QTEST_GUILESS_MAIN(CurveTessellationTests)
#include "curve_tessellation_tests.moc"
