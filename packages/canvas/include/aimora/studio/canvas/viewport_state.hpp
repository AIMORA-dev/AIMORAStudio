// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace aimora::studio::canvas {

struct ViewportState final {
    QPointF center;
    double zoom{1.0};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] QRectF visibleSceneRect(const QSizeF& viewportPixels) const noexcept;
};

} // namespace aimora::studio::canvas
