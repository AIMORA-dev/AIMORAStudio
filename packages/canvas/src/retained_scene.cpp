// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/retained_scene.hpp"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace aimora::studio::canvas {
namespace {

[[nodiscard]] bool finitePoint(const QPointF& point) noexcept {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

[[nodiscard]] bool finiteRect(const QRectF& rect) noexcept {
    return finitePoint(rect.topLeft()) && finitePoint(rect.bottomRight());
}

[[nodiscard]] QRectF segmentBounds(const QPointF& start, const QPointF& end, const qreal width) {
    const qreal margin = std::max<qreal>(0.001, width / 2.0);
    return QRectF{start, end}.normalized().adjusted(-margin, -margin, margin, margin);
}

[[nodiscard]] QRectF bufferBounds(const SegmentBuffer& buffer) {
    QRectF result;
    for (const QRectF& bounds : buffer.bounds) {
        result = result.isValid() ? result.united(bounds) : bounds;
    }
    return result;
}

bool appendSegment(SegmentBuffer& target,
                   qsizetype& compiledSegmentCount,
                   const QPointF& start,
                   const QPointF& end,
                   const quint32 styleIndex,
                   const SceneItemId id,
                   const QVector<SceneStyle>& styles,
                   const SceneLimits& limits,
                   QVector<SceneDiagnostic>& diagnostics) {
    if (compiledSegmentCount >= limits.maximumCompiledSegments) {
        diagnostics.push_back({
            QStringLiteral("scene.segment_limit"),
            QStringLiteral("compiled scene exceeds the segment limit"),
            id,
        });
        return false;
    }
    if (!finitePoint(start) || !finitePoint(end)) {
        diagnostics.push_back({
            QStringLiteral("scene.nonfinite_point"),
            QStringLiteral("primitive contains a non-finite point"),
            id,
        });
        return false;
    }
    if (styleIndex >= static_cast<quint32>(styles.size())) {
        diagnostics.push_back({
            QStringLiteral("scene.style_missing"),
            QStringLiteral("primitive references an unavailable style"),
            id,
        });
        return false;
    }
    const QRectF bounds = segmentBounds(start, end, styles[styleIndex].strokeWidth);
    if (!finiteRect(bounds) || !bounds.isValid()) {
        diagnostics.push_back({
            QStringLiteral("scene.nonfinite_bounds"),
            QStringLiteral("primitive extent cannot be represented by finite valid bounds"),
            id,
        });
        return false;
    }
    target.starts.push_back(start);
    target.ends.push_back(end);
    target.styleIndices.push_back(styleIndex);
    target.ids.push_back(id);
    target.bounds.push_back(bounds);
    ++compiledSegmentCount;
    return true;
}

bool compilePrimitive(const PrimitiveVector& primitive,
                      SegmentBuffer& target,
                      qsizetype& compiledSegmentCount,
                      const QVector<SceneStyle>& styles,
                      const SceneLimits& limits,
                      QVector<SceneDiagnostic>& diagnostics) {
    auto failShape = [&diagnostics, &primitive](const QString& message) {
        diagnostics.push_back({QStringLiteral("scene.invalid_primitive"), message, primitive.id});
        return false;
    };
    if (primitive.id == 0) {
        return failShape(QStringLiteral("primitive requires a non-zero stable id"));
    }
    const auto fitsSegmentBudget = [&](const qsizetype required) {
        if (required <= limits.maximumCompiledSegments - compiledSegmentCount) {
            return true;
        }
        diagnostics.push_back({QStringLiteral("scene.segment_limit"),
                               QStringLiteral("compiled scene exceeds the segment limit"),
                               primitive.id});
        return false;
    };
    const auto append = [&](const QPointF& start, const QPointF& end) {
        return appendSegment(target,
                             compiledSegmentCount,
                             start,
                             end,
                             primitive.styleIndex,
                             primitive.id,
                             styles,
                             limits,
                             diagnostics);
    };

    if (primitive.kind == PrimitiveKind::Line) {
        return primitive.points.size() == 2
                   ? append(primitive.points[0], primitive.points[1])
                   : failShape(QStringLiteral("line requires exactly two points"));
    }
    if (primitive.kind == PrimitiveKind::Polyline || primitive.kind == PrimitiveKind::Polygon) {
        const qsizetype minimum = primitive.kind == PrimitiveKind::Polygon ? 3 : 2;
        if (primitive.points.size() < minimum) {
            return failShape(QStringLiteral("polyline or polygon has too few points"));
        }
        const qsizetype required = primitive.points.size() - 1 +
                                   (primitive.kind == PrimitiveKind::Polygon ? 1 : 0);
        if (!fitsSegmentBudget(required)) {
            return false;
        }
        for (qsizetype index = 1; index < primitive.points.size(); ++index) {
            if (!append(primitive.points[index - 1], primitive.points[index])) {
                return false;
            }
        }
        return primitive.kind != PrimitiveKind::Polygon ||
               append(primitive.points.back(), primitive.points.front());
    }
    if (primitive.kind != PrimitiveKind::Arc && primitive.kind != PrimitiveKind::Circle &&
        primitive.kind != PrimitiveKind::Ellipse) {
        return failShape(QStringLiteral("primitive kind is not supported"));
    }
    if (primitive.points.size() != 1 || !finitePoint(primitive.points.front())) {
        return failShape(QStringLiteral("radial primitive requires one finite center"));
    }
    qreal radiusX = primitive.radii.width();
    qreal radiusY = primitive.radii.height();
    qreal start = primitive.startDegrees;
    qreal sweep = primitive.sweepDegrees;
    if (primitive.kind == PrimitiveKind::Circle) {
        radiusY = radiusX;
        start = 0.0;
        sweep = 360.0;
    } else if (primitive.kind == PrimitiveKind::Ellipse) {
        start = 0.0;
        sweep = 360.0;
    }
    if (!std::isfinite(radiusX) || !std::isfinite(radiusY) || radiusX <= 0.0 || radiusY <= 0.0 ||
        !std::isfinite(start) || !std::isfinite(sweep) || sweep == 0.0 || std::abs(sweep) > 360.0) {
        return failShape(QStringLiteral("radial primitive has invalid radii or angles"));
    }
    start = std::remainder(start, 360.0);
    const int segmentCount =
        std::max(1,
                 static_cast<int>(std::ceil((std::abs(sweep) / 360.0) *
                                            static_cast<qreal>(limits.arcSegmentsPerCircle))));
    const QPointF center = primitive.points.front();
    if (!fitsSegmentBudget(segmentCount)) {
        return false;
    }
    auto radialPoint = [&](const int index) {
        const qreal fraction = static_cast<qreal>(index) / static_cast<qreal>(segmentCount);
        const qreal radians = (start + (sweep * fraction)) * std::numbers::pi_v<qreal> / 180.0;
        return QPointF{
            center.x() + (std::cos(radians) * radiusX),
            center.y() + (std::sin(radians) * radiusY),
        };
    };
    const QPointF first = radialPoint(0);
    QPointF previous = first;
    for (int index = 1; index <= segmentCount; ++index) {
        const QPointF current = index == segmentCount && std::abs(sweep) == 360.0
                                    ? first : radialPoint(index);
        if (!append(previous, current)) {
            return false;
        }
        previous = current;
    }
    return true;
}

[[nodiscard]] qsizetype estimateSceneBytes(const QVector<SceneStyle>& styles,
                                           const SegmentBuffer& segments,
                                           const QVector<SymbolGeometry>& geometries,
                                           const QVector<SymbolInstance>& instances,
                                           const QVector<SceneText>& texts,
                                           const std::optional<ScenePage>& page,
                                           const QVector<SceneOverlay>& overlays,
                                           const SceneSpatialIndex& index) {
    qsizetype bytes = styles.size() * static_cast<qsizetype>(sizeof(SceneStyle));
    bytes += segments.estimatedBytes();
    for (const SymbolGeometry& geometry : geometries) {
        bytes += geometry.detail.estimatedBytes() + geometry.compact.estimatedBytes();
    }
    bytes += instances.size() * static_cast<qsizetype>(sizeof(SymbolInstance));
    bytes += overlays.size() * static_cast<qsizetype>(sizeof(SceneOverlay));
    bytes += page.has_value() ? static_cast<qsizetype>(sizeof(ScenePage)) : 0;
    bytes += index.estimatedBytes();
    for (const SceneText& text : texts) {
        bytes += static_cast<qsizetype>(sizeof(SceneText)) +
                 (text.text.size() * static_cast<qsizetype>(sizeof(QChar))) +
                 (text.fontFamily.size() * static_cast<qsizetype>(sizeof(QChar)));
    }
    return bytes;
}

} // namespace

bool SceneStyle::isValid() const noexcept {
    return stroke.isValid() && fill.isValid() && std::isfinite(strokeWidth) && strokeWidth > 0.0;
}

bool SceneLimits::isValid() const noexcept {
    return maximumSourcePrimitives > 0 && maximumCompiledSegments > 0 &&
           maximumSymbolInstances > 0 && maximumTextRuns > 0 && maximumSpatialRecords > 0 &&
           maximumBytes > 0 && arcSegmentsPerCircle >= 8 && arcSegmentsPerCircle <= 256;
}

bool SegmentBuffer::isValid(const qsizetype styleCount) const noexcept {
    const qsizetype count = starts.size();
    if (ends.size() != count || styleIndices.size() != count || ids.size() != count ||
        bounds.size() != count) {
        return false;
    }
    for (qsizetype index = 0; index < count; ++index) {
        if (!finitePoint(starts[index]) || !finitePoint(ends[index]) ||
            styleIndices[index] >= static_cast<quint32>(styleCount) ||
            !finiteRect(bounds[index]) || !bounds[index].isValid()) {
            return false;
        }
    }
    return true;
}

qsizetype SegmentBuffer::size() const noexcept {
    return starts.size();
}

qsizetype SegmentBuffer::estimatedBytes() const noexcept {
    return size() * static_cast<qsizetype>((2 * sizeof(QPointF)) + sizeof(quint32) +
                                           sizeof(SceneItemId) + sizeof(QRectF));
}

RetainedScene::RetainedScene(const quint64 generation,
                             QVector<SceneStyle> styles,
                             SegmentBuffer segments,
                             QVector<SymbolGeometry> symbolGeometries,
                             QVector<SymbolInstance> symbolInstances,
                             QVector<SceneText> texts,
                             std::optional<ScenePage> page,
                             QVector<SceneOverlay> overlays,
                             SceneSpatialIndex spatialIndex,
                             SceneStatistics statistics)
    : generation_{generation}, styles_{std::move(styles)}, segments_{std::move(segments)},
      symbolGeometries_{std::move(symbolGeometries)}, symbolInstances_{std::move(symbolInstances)},
      texts_{std::move(texts)}, page_{page}, overlays_{std::move(overlays)},
      spatialIndex_{std::move(spatialIndex)}, statistics_{statistics} {}

quint64 RetainedScene::generation() const noexcept {
    return generation_;
}
const QVector<SceneStyle>& RetainedScene::styles() const noexcept {
    return styles_;
}
const SegmentBuffer& RetainedScene::segments() const noexcept {
    return segments_;
}
const QVector<SymbolGeometry>& RetainedScene::symbolGeometries() const noexcept {
    return symbolGeometries_;
}
const QVector<SymbolInstance>& RetainedScene::symbolInstances() const noexcept {
    return symbolInstances_;
}
const QVector<SceneText>& RetainedScene::texts() const noexcept {
    return texts_;
}
const std::optional<ScenePage>& RetainedScene::page() const noexcept {
    return page_;
}
const QVector<SceneOverlay>& RetainedScene::overlays() const noexcept {
    return overlays_;
}
const SceneSpatialIndex& RetainedScene::spatialIndex() const noexcept {
    return spatialIndex_;
}
const SceneStatistics& RetainedScene::statistics() const noexcept {
    return statistics_;
}

bool RetainedScene::isValid() const noexcept {
    if (styles_.isEmpty() || !std::all_of(styles_.begin(), styles_.end(), [](const auto& style) {
            return style.isValid();
        })) {
        return false;
    }
    if (!segments_.isValid(styles_.size()) || !spatialIndex_.isValid()) {
        return false;
    }
    for (const SymbolGeometry& geometry : symbolGeometries_) {
        if (geometry.key == 0 || !geometry.detail.isValid(styles_.size()) ||
            !geometry.compact.isValid(styles_.size()) || !geometry.bounds.isValid()) {
            return false;
        }
    }
    for (const SymbolInstance& instance : symbolInstances_) {
        if (instance.geometryIndex < 0 || instance.geometryIndex >= symbolGeometries_.size() ||
            !finiteRect(instance.bounds) || !instance.bounds.isValid()) {
            return false;
        }
    }
    qsizetype compiledSegmentCount = segments_.size();
    for (const SymbolGeometry& geometry : symbolGeometries_) {
        compiledSegmentCount += geometry.detail.size() + geometry.compact.size();
    }
    return statistics_.segmentCount == compiledSegmentCount &&
           statistics_.symbolGeometryCount == symbolGeometries_.size() &&
           statistics_.symbolInstanceCount == symbolInstances_.size() &&
           statistics_.textRunCount == texts_.size() &&
           statistics_.spatialRecordCount == spatialIndex_.recordCount() &&
           statistics_.estimatedBytes > 0;
}

bool SceneCompileResult::succeeded() const noexcept {
    return scene != nullptr && diagnostics.isEmpty() && scene->isValid();
}

const SegmentBuffer& symbolSegments(const SymbolGeometry& geometry,
                                    const SemanticLevelOfDetail level) noexcept {
    if (level == SemanticLevelOfDetail::Compact && !geometry.compact.starts.isEmpty()) {
        return geometry.compact;
    }
    return geometry.detail;
}

SemanticLevelOfDetail selectSymbolLevelOfDetail(const SymbolInstance& instance,
                                                const qreal zoom,
                                                const qreal compactThresholdPixels) noexcept {
    if (!std::isfinite(zoom) || zoom <= 0.0 || !std::isfinite(compactThresholdPixels) ||
        compactThresholdPixels <= 0.0) {
        return SemanticLevelOfDetail::Compact;
    }
    const qreal projected = std::max(instance.bounds.width(), instance.bounds.height()) * zoom;
    return projected < compactThresholdPixels ? SemanticLevelOfDetail::Compact
                                              : SemanticLevelOfDetail::Detail;
}

SceneCompileResult RetainedSceneCompiler::compile(const SceneDocument& document,
                                                  const SceneLimits& limits) const {
    SceneCompileResult result;
    if (!limits.isValid()) {
        result.diagnostics.push_back({QStringLiteral("scene.invalid_limits"),
                                      QStringLiteral("scene limits are invalid"),
                                      0});
        return result;
    }
    if (document.styles.isEmpty() ||
        !std::all_of(document.styles.begin(), document.styles.end(), [](const auto& style) {
            return style.isValid();
        })) {
        result.diagnostics.push_back({QStringLiteral("scene.invalid_styles"),
                                      QStringLiteral("scene requires valid styles"),
                                      0});
        return result;
    }
    qsizetype sourceCount = document.primitives.size();
    for (const SymbolGeometrySource& geometry : document.symbolGeometries) {
        if (sourceCount > limits.maximumSourcePrimitives - geometry.detail.size() ||
            sourceCount + geometry.detail.size() >
                limits.maximumSourcePrimitives - geometry.compact.size()) {
            result.diagnostics.push_back({QStringLiteral("scene.source_limit"),
                                          QStringLiteral("scene source exceeds configured limits"),
                                          0});
            return result;
        }
        sourceCount += geometry.detail.size() + geometry.compact.size();
    }
    if (sourceCount > limits.maximumSourcePrimitives ||
        document.symbolInstances.size() > limits.maximumSymbolInstances ||
        document.texts.size() > limits.maximumTextRuns) {
        result.diagnostics.push_back({QStringLiteral("scene.source_limit"),
                                      QStringLiteral("scene source exceeds configured limits"),
                                      0});
        return result;
    }

    QSet<SceneItemId> sourceIds;
    auto reserveId = [&](const SceneItemId id, const QString& role) {
        if (id == 0 || sourceIds.contains(id)) {
            result.diagnostics.push_back({QStringLiteral("scene.id_conflict"),
                                          role + QStringLiteral(" has a zero or duplicate id"),
                                          id});
            return false;
        }
        sourceIds.insert(id);
        return true;
    };

    SegmentBuffer segments;
    qsizetype compiledSegmentCount = 0;
    for (const PrimitiveVector& primitive : document.primitives) {
        if (!reserveId(primitive.id, QStringLiteral("primitive")) ||
            !compilePrimitive(primitive,
                              segments,
                              compiledSegmentCount,
                              document.styles,
                              limits,
                              result.diagnostics)) {
            return result;
        }
    }

    QVector<SymbolGeometry> geometries;
    geometries.reserve(document.symbolGeometries.size());
    QHash<quint64, qsizetype> geometryIndices;
    for (const SymbolGeometrySource& source : document.symbolGeometries) {
        if (source.key == 0 || geometryIndices.contains(source.key) || source.detail.isEmpty()) {
            result.diagnostics.push_back(
                {QStringLiteral("scene.symbol_geometry_conflict"),
                 QStringLiteral(
                     "symbol geometry key must be unique and detail geometry cannot be empty"),
                 0});
            return result;
        }
        SymbolGeometry geometry;
        geometry.key = source.key;
        for (const PrimitiveVector& primitive : source.detail) {
            if (!compilePrimitive(primitive,
                                  geometry.detail,
                                  compiledSegmentCount,
                                  document.styles,
                                  limits,
                                  result.diagnostics)) {
                return result;
            }
        }
        for (const PrimitiveVector& primitive : source.compact) {
            if (!compilePrimitive(primitive,
                                  geometry.compact,
                                  compiledSegmentCount,
                                  document.styles,
                                  limits,
                                  result.diagnostics)) {
                return result;
            }
        }
        geometry.bounds = bufferBounds(geometry.detail);
        if (!geometry.bounds.isValid()) {
            result.diagnostics.push_back({QStringLiteral("scene.symbol_geometry_empty"),
                                          QStringLiteral("symbol detail geometry has no bounds"),
                                          0});
            return result;
        }
        geometryIndices.insert(source.key, geometries.size());
        geometries.push_back(std::move(geometry));
    }

    QVector<SymbolInstance> instances;
    instances.reserve(document.symbolInstances.size());
    for (const SymbolInstanceSource& source : document.symbolInstances) {
        if (!reserveId(source.id, QStringLiteral("symbol instance")) ||
            !geometryIndices.contains(source.geometryKey) || !source.transform.isInvertible()) {
            result.diagnostics.push_back(
                {QStringLiteral("scene.symbol_instance_invalid"),
                 QStringLiteral("symbol instance has missing geometry or invalid transform"),
                 source.id});
            return result;
        }
        const qsizetype geometryIndex = geometryIndices.value(source.geometryKey);
        const QRectF bounds = source.transform.mapRect(geometries[geometryIndex].bounds);
        if (!bounds.isValid() || !finiteRect(bounds)) {
            result.diagnostics.push_back({QStringLiteral("scene.symbol_instance_bounds"),
                                          QStringLiteral("symbol instance produces invalid bounds"),
                                          source.id});
            return result;
        }
        instances.push_back({source.id,
                             geometryIndex,
                             source.transform,
                             bounds,
                             source.selected,
                             source.diagnostic});
    }

    QVector<SceneText> texts;
    texts.reserve(document.texts.size());
    for (const SceneTextSource& source : document.texts) {
        if (!reserveId(source.id, QStringLiteral("text")) || source.text.isEmpty() ||
            !finitePoint(source.position) || !std::isfinite(source.pointSize) ||
            source.pointSize <= 0.0 ||
            source.styleIndex >= static_cast<quint32>(document.styles.size())) {
            result.diagnostics.push_back({QStringLiteral("scene.text_invalid"),
                                          QStringLiteral("text run is invalid"),
                                          source.id});
            return result;
        }
        if (source.bounds.isValid() && !finiteRect(source.bounds)) {
            result.diagnostics.push_back({QStringLiteral("scene.text_bounds"),
                                          QStringLiteral("text run has non-finite bounds"),
                                          source.id});
            return result;
        }
        const QRectF bounds = source.bounds.isValid()
                                  ? source.bounds
                                  : QRectF{source.position.x(),
                                           source.position.y() - source.pointSize,
                                           std::max<qreal>(source.pointSize,
                                                           static_cast<qreal>(source.text.size()) *
                                                               source.pointSize * 0.6),
                                           source.pointSize * 1.3};
        texts.push_back({source.id,
                         source.text,
                         source.position,
                         source.fontFamily,
                         source.pointSize,
                         source.styleIndex,
                         bounds});
    }

    std::optional<ScenePage> page;
    if (document.page.has_value()) {
        const auto& source = document.page.value();
        if (!reserveId(source.id, QStringLiteral("page")) || !source.bounds.isValid() ||
            !finiteRect(source.bounds) || !source.paper.isValid()) {
            result.diagnostics.push_back({QStringLiteral("scene.page_invalid"),
                                          QStringLiteral("page geometry is invalid"),
                                          source.id});
            return result;
        }
        page = ScenePage{source.id, source.bounds, source.paper};
    }

    QVector<SceneOverlay> overlays;
    overlays.reserve(document.overlays.size());
    for (const SceneOverlaySource& source : document.overlays) {
        if (!reserveId(source.id, QStringLiteral("overlay")) || !source.bounds.isValid() ||
            !finiteRect(source.bounds) || !source.color.isValid() || !std::isfinite(source.width) ||
            source.width <= 0.0) {
            result.diagnostics.push_back({QStringLiteral("scene.overlay_invalid"),
                                          QStringLiteral("overlay is invalid"),
                                          source.id});
            return result;
        }
        overlays.push_back({source.id, source.bounds, source.color, source.width});
    }

    QVector<SpatialRecord> records;
    records.reserve(segments.size() + instances.size() + texts.size() + overlays.size());
    for (qsizetype index = 0; index < segments.size(); ++index) {
        records.push_back(
            {segments.bounds[index], segments.ids[index], SceneItemKind::Segment, index});
    }
    for (qsizetype index = 0; index < instances.size(); ++index) {
        records.push_back(
            {instances[index].bounds, instances[index].id, SceneItemKind::Symbol, index});
    }
    for (qsizetype index = 0; index < texts.size(); ++index) {
        records.push_back({texts[index].bounds, texts[index].id, SceneItemKind::Text, index});
    }
    for (qsizetype index = 0; index < overlays.size(); ++index) {
        records.push_back(
            {overlays[index].bounds, overlays[index].id, SceneItemKind::Overlay, index});
    }
    if (records.size() > limits.maximumSpatialRecords) {
        result.diagnostics.push_back(
            {QStringLiteral("scene.spatial_limit"),
             QStringLiteral("scene spatial index exceeds configured limits"),
             0});
        return result;
    }
    SceneSpatialIndex spatialIndex{records};
    if (!spatialIndex.isValid()) {
        result.diagnostics.push_back({QStringLiteral("scene.spatial_bounds"),
                                      QStringLiteral("scene spatial extent is not finite"),
                                      0});
        return result;
    }
    SceneStatistics statistics;
    statistics.sourcePrimitiveCount = sourceCount;
    statistics.segmentCount = compiledSegmentCount;
    statistics.symbolGeometryCount = geometries.size();
    statistics.symbolInstanceCount = instances.size();
    statistics.textRunCount = texts.size();
    statistics.spatialRecordCount = spatialIndex.recordCount();
    statistics.estimatedBytes = estimateSceneBytes(
        document.styles, segments, geometries, instances, texts, page, overlays, spatialIndex);
    if (statistics.estimatedBytes > limits.maximumBytes) {
        result.diagnostics.push_back({QStringLiteral("scene.memory_limit"),
                                      QStringLiteral("scene exceeds configured memory budget"),
                                      0});
        return result;
    }
    result.scene = std::make_shared<const RetainedScene>(document.generation,
                                                         document.styles,
                                                         std::move(segments),
                                                         std::move(geometries),
                                                         std::move(instances),
                                                         std::move(texts),
                                                         page,
                                                         std::move(overlays),
                                                         std::move(spatialIndex),
                                                         statistics);
    if (!result.scene->isValid()) {
        result.diagnostics.push_back(
            {QStringLiteral("scene.internal_invalid"),
             QStringLiteral("compiled retained scene failed its invariant"),
             0});
        result.scene.reset();
    }
    return result;
}

} // namespace aimora::studio::canvas
