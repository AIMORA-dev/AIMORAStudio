// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/spatial_index.hpp"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace aimora::studio::canvas {
namespace {

constexpr qsizetype leafCapacity = 8;

[[nodiscard]] QRectF
mergedBounds(const QVector<SpatialRecord>& records, const qsizetype begin, const qsizetype end) {
    QRectF result;
    for (qsizetype index = begin; index < end; ++index) {
        result = result.isValid() ? result.united(records[index].bounds) : records[index].bounds;
    }
    return result;
}

} // namespace

SceneSpatialIndex::SceneSpatialIndex(QVector<SpatialRecord> records)
    : records_{std::move(records)} {
    if (!records_.isEmpty()) {
        nodes_.reserve((records_.size() * 2) - 1);
        buildNode(0, records_.size(), 0);
    }
}

qsizetype
SceneSpatialIndex::buildNode(const qsizetype begin, const qsizetype end, const int depth) {
    const qsizetype nodeIndex = nodes_.size();
    nodes_.push_back(Node{});
    Node node;
    node.bounds = mergedBounds(records_, begin, end);
    node.begin = begin;
    node.count = end - begin;

    if (node.count > leafCapacity) {
        const qsizetype middle = begin + (node.count / 2);
        const bool splitX = (depth % 2) == 0;
        auto first = records_.begin() + begin;
        auto median = records_.begin() + middle;
        auto last = records_.begin() + end;
        std::nth_element(
            first, median, last, [splitX](const SpatialRecord& left, const SpatialRecord& right) {
                const qreal leftCenter =
                    splitX ? left.bounds.center().x() : left.bounds.center().y();
                const qreal rightCenter =
                    splitX ? right.bounds.center().x() : right.bounds.center().y();
                if (leftCenter != rightCenter) {
                    return leftCenter < rightCenter;
                }
                if (left.id != right.id) {
                    return left.id < right.id;
                }
                return left.index < right.index;
            });
        node.left = buildNode(begin, middle, depth + 1);
        node.right = buildNode(middle, end, depth + 1);
        node.begin = 0;
        node.count = 0;
    }
    nodes_[nodeIndex] = node;
    return nodeIndex;
}

bool SceneSpatialIndex::isValid() const noexcept {
    if (records_.isEmpty()) {
        return nodes_.isEmpty();
    }
    return !nodes_.isEmpty() && nodes_.size() <= (records_.size() * 2) - 1;
}

qsizetype SceneSpatialIndex::recordCount() const noexcept {
    return records_.size();
}

qsizetype SceneSpatialIndex::nodeCount() const noexcept {
    return nodes_.size();
}

QVector<SpatialRecord> SceneSpatialIndex::query(const QRectF& area) const {
    QVector<SpatialRecord> result;
    if (nodes_.isEmpty() || !area.isValid()) {
        return result;
    }
    QVector<qsizetype> stack{0};
    while (!stack.isEmpty()) {
        const Node& node = nodes_[stack.takeLast()];
        if (!node.bounds.intersects(area)) {
            continue;
        }
        if (node.left >= 0) {
            stack.push_back(node.right);
            stack.push_back(node.left);
            continue;
        }
        for (qsizetype offset = 0; offset < node.count; ++offset) {
            const SpatialRecord& record = records_[node.begin + offset];
            if (record.bounds.intersects(area)) {
                result.push_back(record);
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.kind != right.kind) {
            return left.kind < right.kind;
        }
        if (left.id != right.id) {
            return left.id < right.id;
        }
        return left.index < right.index;
    });
    return result;
}

QVector<SceneItemId> SceneSpatialIndex::hitTest(const QPointF& point, const qreal tolerance) const {
    if (!std::isfinite(tolerance) || tolerance < 0.0) {
        return {};
    }
    const QRectF area{
        point.x() - tolerance,
        point.y() - tolerance,
        tolerance * 2.0,
        tolerance * 2.0,
    };
    QSet<SceneItemId> unique;
    for (const SpatialRecord& record : query(area.adjusted(-0.001, -0.001, 0.001, 0.001))) {
        unique.insert(record.id);
    }
    QVector<SceneItemId> result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace aimora::studio::canvas
