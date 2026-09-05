// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/canvas/retained_scene.hpp"
#include "aimora/studio/canvas/viewport_state.hpp"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QSize>

namespace aimora::studio::canvas {

struct RenderPalette final {
    QColor canvas{QStringLiteral("#f4f1e8")};
    QColor gridMinor{QStringLiteral("#d9d5ca")};
    QColor gridMajor{QStringLiteral("#b8b2a5")};
    QColor selection{QStringLiteral("#006f8f")};
    QColor diagnostic{QStringLiteral("#b3261e")};
    QColor text{QStringLiteral("#172126")};
    bool gridVisible{true};
    qreal gridSpacing{24.0};

    [[nodiscard]] bool isValid() const noexcept;
};

enum class PainterRenderPass : std::uint8_t {
    Complete,
    TextAndDiagnostics,
};

struct RenderStatistics final {
    qsizetype spatialCandidates{0};
    qsizetype renderedSegments{0};
    qsizetype renderedSymbols{0};
    qsizetype renderedTexts{0};
    qsizetype renderedOverlays{0};
};

class PainterSceneRenderer final {
  public:
    [[nodiscard]] RenderStatistics
    render(QPainter& painter,
           const RetainedScene& scene,
           const ViewportState& viewport,
           const QSizeF& viewportPixels,
           const RenderPalette& palette,
           PainterRenderPass pass = PainterRenderPass::Complete) const;

    [[nodiscard]] QImage renderReferenceImage(const RetainedScene& scene,
                                              const ViewportState& viewport,
                                              const QSize& pixelSize,
                                              const RenderPalette& palette,
                                              qreal devicePixelRatio = 1.0) const;
};

} // namespace aimora::studio::canvas
