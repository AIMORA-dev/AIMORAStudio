// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/painter_scene_renderer.hpp"

#include <QFont>
#include <QLineF>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <limits>

namespace aimora::studio::canvas {
namespace {

constexpr qreal baseGridSpacing = 24.0;
constexpr int majorGridInterval = 5;
constexpr int maximumGridLines = 512;

void drawGrid(QPainter& painter,
              const QRectF& visible,
              const qreal zoom,
              const RenderPalette& palette) {
    qreal spacing = baseGridSpacing;
    while (spacing * zoom < 12.0) {
        spacing *= static_cast<qreal>(majorGridInterval);
    }
    const qreal firstX = std::floor(visible.left() / spacing) * spacing;
    const qreal firstY = std::floor(visible.top() / spacing) * spacing;
    int count = 0;
    QPen pen;
    pen.setCosmetic(true);
    for (qreal x = firstX; x <= visible.right() && count < maximumGridLines;
         x += spacing, ++count) {
        const auto ordinal = static_cast<qint64>(std::llround(x / spacing));
        pen.setColor((ordinal % majorGridInterval) == 0 ? palette.gridMajor : palette.gridMinor);
        painter.setPen(pen);
        painter.drawLine(QLineF{x, visible.top(), x, visible.bottom()});
    }
    count = 0;
    for (qreal y = firstY; y <= visible.bottom() && count < maximumGridLines;
         y += spacing, ++count) {
        const auto ordinal = static_cast<qint64>(std::llround(y / spacing));
        pen.setColor((ordinal % majorGridInterval) == 0 ? palette.gridMajor : palette.gridMinor);
        painter.setPen(pen);
        painter.drawLine(QLineF{visible.left(), y, visible.right(), y});
    }
}

void configurePen(QPainter& painter, const SceneStyle& style) {
    QPen pen{style.stroke};
    pen.setWidthF(style.strokeWidth);
    pen.setStyle(style.penStyle);
    pen.setCosmetic(false);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
}

void drawSegmentBuffer(QPainter& painter,
                       const SegmentBuffer& buffer,
                       const QVector<SceneStyle>& styles,
                       qsizetype& renderedSegments) {
    quint32 activeStyle = std::numeric_limits<quint32>::max();
    for (qsizetype index = 0; index < buffer.size(); ++index) {
        if (buffer.styleIndices[index] != activeStyle) {
            activeStyle = buffer.styleIndices[index];
            configurePen(painter, styles[activeStyle]);
        }
        painter.drawLine(QLineF{buffer.starts[index], buffer.ends[index]});
        ++renderedSegments;
    }
}

} // namespace

bool RenderPalette::isValid() const noexcept {
    return canvas.isValid() && gridMinor.isValid() && gridMajor.isValid() && selection.isValid() &&
           diagnostic.isValid() && text.isValid();
}

RenderStatistics PainterSceneRenderer::render(QPainter& painter,
                                              const RetainedScene& scene,
                                              const ViewportState& viewport,
                                              const QSizeF& viewportPixels,
                                              const RenderPalette& palette,
                                              const PainterRenderPass pass) const {
    RenderStatistics statistics;
    if (!scene.isValid() || !viewport.isValid() || !viewportPixels.isValid() ||
        viewportPixels.isEmpty() || !palette.isValid()) {
        return statistics;
    }

    const QRectF visible = viewport.visibleSceneRect(viewportPixels);
    const QVector<SpatialRecord> candidates = scene.spatialIndex().query(visible);
    statistics.spatialCandidates = candidates.size();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    if (pass == PainterRenderPass::Complete) {
        painter.fillRect(QRectF{QPointF{0.0, 0.0}, viewportPixels}, palette.canvas);
    }
    painter.translate(viewportPixels.width() / 2.0, viewportPixels.height() / 2.0);
    painter.scale(viewport.zoom, viewport.zoom);
    painter.translate(-viewport.center.x(), -viewport.center.y());

    if (pass == PainterRenderPass::Complete) {
        if (scene.page().has_value() && scene.page()->bounds.intersects(visible)) {
            painter.fillRect(scene.page()->bounds, scene.page()->paper);
            QPen pagePen{palette.gridMajor};
            pagePen.setCosmetic(true);
            painter.setPen(pagePen);
            painter.drawRect(scene.page()->bounds);
        }
        drawGrid(painter, visible, viewport.zoom, palette);
        quint32 activeStyle = std::numeric_limits<quint32>::max();
        for (const SpatialRecord& record : candidates) {
            if (record.kind != SceneItemKind::Segment) {
                continue;
            }
            const SegmentBuffer& segments = scene.segments();
            if (segments.styleIndices[record.index] != activeStyle) {
                activeStyle = segments.styleIndices[record.index];
                configurePen(painter, scene.styles()[activeStyle]);
            }
            painter.drawLine(QLineF{segments.starts[record.index], segments.ends[record.index]});
            ++statistics.renderedSegments;
        }
        for (const SpatialRecord& record : candidates) {
            if (record.kind != SceneItemKind::Symbol) {
                continue;
            }
            const SymbolInstance& instance = scene.symbolInstances()[record.index];
            const SymbolGeometry& geometry = scene.symbolGeometries()[instance.geometryIndex];
            const auto level = selectSymbolLevelOfDetail(instance, viewport.zoom);
            painter.save();
            painter.setTransform(instance.transform, true);
            drawSegmentBuffer(painter,
                              symbolSegments(geometry, level),
                              scene.styles(),
                              statistics.renderedSegments);
            painter.restore();
            ++statistics.renderedSymbols;
        }
    }

    for (const SpatialRecord& record : candidates) {
        if (record.kind == SceneItemKind::Text) {
            const SceneText& text = scene.texts()[record.index];
            QFont font{text.fontFamily};
            font.setPointSizeF(text.pointSize);
            painter.setFont(font);
            painter.setPen(scene.styles()[text.styleIndex].stroke);
            painter.drawText(text.position, text.text);
            ++statistics.renderedTexts;
        } else if (record.kind == SceneItemKind::Symbol) {
            const SymbolInstance& instance = scene.symbolInstances()[record.index];
            if (instance.selected || instance.diagnostic) {
                QPen pen{instance.diagnostic ? palette.diagnostic : palette.selection};
                pen.setCosmetic(true);
                pen.setWidth(2);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(instance.bounds);
            }
        } else if (record.kind == SceneItemKind::Overlay) {
            const SceneOverlay& overlay = scene.overlays()[record.index];
            QPen pen{overlay.color};
            pen.setWidthF(overlay.width);
            pen.setCosmetic(true);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(overlay.bounds);
            ++statistics.renderedOverlays;
        }
    }
    painter.restore();
    return statistics;
}

QImage PainterSceneRenderer::renderReferenceImage(const RetainedScene& scene,
                                                  const ViewportState& viewport,
                                                  const QSize& pixelSize,
                                                  const RenderPalette& palette,
                                                  const qreal devicePixelRatio) const {
    if (pixelSize.isEmpty() || !std::isfinite(devicePixelRatio) || devicePixelRatio <= 0.0) {
        return {};
    }
    const QSize physicalSize{
        qRound(pixelSize.width() * devicePixelRatio),
        qRound(pixelSize.height() * devicePixelRatio),
    };
    QImage image{physicalSize, QImage::Format_ARGB32_Premultiplied};
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(palette.canvas);
    QPainter painter{&image};
    (void)render(painter, scene, viewport, QSizeF{pixelSize}, palette);
    painter.end();
    return image;
}

} // namespace aimora::studio::canvas
