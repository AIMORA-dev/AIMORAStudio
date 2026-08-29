// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/viewport_state.hpp"

#include <cmath>

namespace aimora::studio::canvas {

bool ViewportState::isValid() const noexcept {
    return std::isfinite(center.x()) && std::isfinite(center.y()) && std::isfinite(zoom)
        && zoom > 0.0;
}

QRectF ViewportState::visibleSceneRect(const QSizeF& viewportPixels) const noexcept {
    if(!isValid() || !viewportPixels.isValid() || viewportPixels.isEmpty()) {
        return {};
    }

    const QSizeF sceneSize{viewportPixels.width() / zoom, viewportPixels.height() / zoom};
    const QPointF topLeft{center.x() - (sceneSize.width() / 2.0),
                          center.y() - (sceneSize.height() / 2.0)};
    return {topLeft, sceneSize};
}

} // namespace aimora::studio::canvas
