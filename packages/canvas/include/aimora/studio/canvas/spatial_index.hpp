// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QtTypes>
#include <cstdint>

namespace aimora::studio::canvas {

using SceneItemId = quint64;

enum class SceneItemKind : std::uint8_t {
    Segment,
    Symbol,
    Text,
    Page,
    Overlay,
};

struct SpatialRecord final {
    QRectF bounds;
    SceneItemId id{0};
    SceneItemKind kind{SceneItemKind::Segment};
    qsizetype index{0};
};

class SceneSpatialIndex final {
  public:
    SceneSpatialIndex() = default;
    explicit SceneSpatialIndex(QVector<SpatialRecord> records);

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] qsizetype recordCount() const noexcept;
    [[nodiscard]] qsizetype nodeCount() const noexcept;
    [[nodiscard]] qsizetype estimatedBytes() const noexcept;
    [[nodiscard]] QVector<SpatialRecord> query(const QRectF& area) const;
    [[nodiscard]] QVector<SceneItemId> hitTest(const QPointF& point, qreal tolerance) const;

  private:
    struct Node final {
        QRectF bounds;
        qsizetype left{-1};
        qsizetype right{-1};
        qsizetype begin{0};
        qsizetype count{0};
    };

    qsizetype buildNode(qsizetype begin, qsizetype end, int depth);

    QVector<SpatialRecord> records_;
    QVector<Node> nodes_;
};

} // namespace aimora::studio::canvas
