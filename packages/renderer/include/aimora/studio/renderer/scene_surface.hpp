// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/canvas/painter_scene_renderer.hpp"
#include "aimora/studio/canvas/retained_scene.hpp"
#include "aimora/studio/canvas/viewport_state.hpp"

#include <QImage>
#include <QWidget>
#include <cstdint>
#include <memory>

namespace aimora::studio::renderer {

enum class RendererBackend : std::uint8_t {
    Automatic,
    AcceleratedRhi,
    SoftwarePainter,
};

struct SurfaceStatistics final {
    RendererBackend backend{RendererBackend::SoftwarePainter};
    quint64 requestedFrames{0};
    quint64 renderedFrames{0};
    quint64 sceneGeneration{0};
    qsizetype cpuSceneBytes{0};
    qsizetype gpuVertexBytes{0};
    canvas::RenderStatistics lastRender;
};

class SceneSurface final : public QWidget {
  public:
    explicit SceneSurface(RendererBackend requestedBackend = RendererBackend::Automatic,
                          QWidget* parent = nullptr);
    ~SceneSurface() override;

    SceneSurface(const SceneSurface&) = delete;
    SceneSurface& operator=(const SceneSurface&) = delete;

    void setScene(std::shared_ptr<const canvas::RetainedScene> scene);
    [[nodiscard]] std::shared_ptr<const canvas::RetainedScene> scene() const noexcept;

    void setViewport(const canvas::ViewportState& viewport);
    [[nodiscard]] const canvas::ViewportState& viewport() const noexcept;

    void setRenderPalette(const canvas::RenderPalette& palette);
    [[nodiscard]] const canvas::RenderPalette& renderPalette() const noexcept;

    [[nodiscard]] RendererBackend backend() const noexcept;
    [[nodiscard]] const SurfaceStatistics& statistics() const noexcept;
    [[nodiscard]] QVector<canvas::SceneItemId> hitTest(const QPointF& pixelPoint,
                                                       qreal tolerancePixels) const;
    [[nodiscard]] QImage renderReferenceImage(const QSize& pixelSize,
                                              qreal devicePixelRatio = 1.0) const;

  private:
    class Private;
    std::unique_ptr<Private> private_;
};

} // namespace aimora::studio::renderer
