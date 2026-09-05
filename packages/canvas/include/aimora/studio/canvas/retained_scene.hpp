// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/canvas/spatial_index.hpp"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <QVector>
#include <cstdint>
#include <memory>
#include <optional>

namespace aimora::studio::canvas {

enum class PrimitiveKind : std::uint8_t {
    Line,
    Polyline,
    Arc,
    Circle,
    Ellipse,
    Polygon,
};

enum class SemanticLevelOfDetail : std::uint8_t {
    Detail,
    Compact,
};

struct SceneStyle final {
    QColor stroke{Qt::black};
    QColor fill{Qt::transparent};
    qreal strokeWidth{1.0};
    Qt::PenStyle penStyle{Qt::SolidLine};

    [[nodiscard]] bool isValid() const noexcept;
};

struct PrimitiveVector final {
    SceneItemId id{0};
    PrimitiveKind kind{PrimitiveKind::Line};
    QVector<QPointF> points;
    QSizeF radii;
    qreal startDegrees{0.0};
    qreal sweepDegrees{0.0};
    quint32 styleIndex{0};
};

struct SymbolGeometrySource final {
    quint64 key{0};
    QVector<PrimitiveVector> detail;
    QVector<PrimitiveVector> compact;
};

struct SymbolInstanceSource final {
    SceneItemId id{0};
    quint64 geometryKey{0};
    QTransform transform;
    bool selected{false};
    bool diagnostic{false};
};

struct SceneTextSource final {
    SceneItemId id{0};
    QString text;
    QPointF position;
    QString fontFamily;
    qreal pointSize{10.0};
    quint32 styleIndex{0};
    QRectF bounds;
};

struct ScenePageSource final {
    SceneItemId id{0};
    QRectF bounds;
    QColor paper{Qt::white};
};

struct SceneOverlaySource final {
    SceneItemId id{0};
    QRectF bounds;
    QColor color{Qt::red};
    qreal width{1.0};
};

struct SceneDocument final {
    quint64 generation{0};
    QVector<SceneStyle> styles;
    QVector<PrimitiveVector> primitives;
    QVector<SymbolGeometrySource> symbolGeometries;
    QVector<SymbolInstanceSource> symbolInstances;
    QVector<SceneTextSource> texts;
    std::optional<ScenePageSource> page;
    QVector<SceneOverlaySource> overlays;
};

struct SceneLimits final {
    qsizetype maximumSourcePrimitives{2'000'000};
    qsizetype maximumCompiledSegments{4'000'000};
    qsizetype maximumSymbolInstances{250'000};
    qsizetype maximumTextRuns{500'000};
    qsizetype maximumSpatialRecords{5'000'000};
    qsizetype maximumBytes{512 * 1024 * 1024};
    int arcSegmentsPerCircle{128};

    [[nodiscard]] bool isValid() const noexcept;
};

struct SegmentBuffer final {
    QVector<QPointF> starts;
    QVector<QPointF> ends;
    QVector<quint32> styleIndices;
    QVector<SceneItemId> ids;
    QVector<QRectF> bounds;

    [[nodiscard]] bool isValid(qsizetype styleCount) const noexcept;
    [[nodiscard]] qsizetype size() const noexcept;
    [[nodiscard]] qsizetype estimatedBytes() const noexcept;
};

struct SymbolGeometry final {
    quint64 key{0};
    SegmentBuffer detail;
    SegmentBuffer compact;
    QRectF bounds;
};

struct SymbolInstance final {
    SceneItemId id{0};
    qsizetype geometryIndex{0};
    QTransform transform;
    QRectF bounds;
    bool selected{false};
    bool diagnostic{false};
};

struct SceneText final {
    SceneItemId id{0};
    QString text;
    QPointF position;
    QString fontFamily;
    qreal pointSize{10.0};
    quint32 styleIndex{0};
    QRectF bounds;
};

struct ScenePage final {
    SceneItemId id{0};
    QRectF bounds;
    QColor paper{Qt::white};
};

struct SceneOverlay final {
    SceneItemId id{0};
    QRectF bounds;
    QColor color{Qt::red};
    qreal width{1.0};
};

struct SceneStatistics final {
    qsizetype sourcePrimitiveCount{0};
    qsizetype segmentCount{0};
    qsizetype symbolGeometryCount{0};
    qsizetype symbolInstanceCount{0};
    qsizetype textRunCount{0};
    qsizetype spatialRecordCount{0};
    qsizetype estimatedBytes{0};
};

class RetainedScene final {
  public:
    RetainedScene(quint64 generation,
                  QVector<SceneStyle> styles,
                  SegmentBuffer segments,
                  QVector<SymbolGeometry> symbolGeometries,
                  QVector<SymbolInstance> symbolInstances,
                  QVector<SceneText> texts,
                  std::optional<ScenePage> page,
                  QVector<SceneOverlay> overlays,
                  SceneSpatialIndex spatialIndex,
                  SceneStatistics statistics);

    [[nodiscard]] quint64 generation() const noexcept;
    [[nodiscard]] const QVector<SceneStyle>& styles() const noexcept;
    [[nodiscard]] const SegmentBuffer& segments() const noexcept;
    [[nodiscard]] const QVector<SymbolGeometry>& symbolGeometries() const noexcept;
    [[nodiscard]] const QVector<SymbolInstance>& symbolInstances() const noexcept;
    [[nodiscard]] const QVector<SceneText>& texts() const noexcept;
    [[nodiscard]] const std::optional<ScenePage>& page() const noexcept;
    [[nodiscard]] const QVector<SceneOverlay>& overlays() const noexcept;
    [[nodiscard]] const SceneSpatialIndex& spatialIndex() const noexcept;
    [[nodiscard]] const SceneStatistics& statistics() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;

  private:
    quint64 generation_{0};
    QVector<SceneStyle> styles_;
    SegmentBuffer segments_;
    QVector<SymbolGeometry> symbolGeometries_;
    QVector<SymbolInstance> symbolInstances_;
    QVector<SceneText> texts_;
    std::optional<ScenePage> page_;
    QVector<SceneOverlay> overlays_;
    SceneSpatialIndex spatialIndex_;
    SceneStatistics statistics_;
};

struct SceneDiagnostic final {
    QString code;
    QString message;
    SceneItemId itemId{0};
};

struct SceneCompileResult final {
    std::shared_ptr<const RetainedScene> scene;
    QVector<SceneDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept;
};

class RetainedSceneCompiler final {
  public:
    [[nodiscard]] SceneCompileResult compile(const SceneDocument& document,
                                             const SceneLimits& limits = {}) const;
};

[[nodiscard]] const SegmentBuffer& symbolSegments(const SymbolGeometry& geometry,
                                                  SemanticLevelOfDetail level) noexcept;

[[nodiscard]] SemanticLevelOfDetail selectSymbolLevelOfDetail(
    const SymbolInstance& instance, qreal zoom, qreal compactThresholdPixels = 24.0) noexcept;

} // namespace aimora::studio::canvas
