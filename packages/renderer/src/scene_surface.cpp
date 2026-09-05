// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/renderer/scene_surface.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QRhiWidget>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <rhi/qrhi.h>
#include <utility>
#include <vector>

namespace aimora::studio::renderer {
namespace {

using canvas::PainterRenderPass;
using canvas::RenderPalette;
using canvas::RetainedScene;
using canvas::SceneItemKind;
using canvas::SegmentBuffer;
using canvas::ViewportState;

constexpr qsizetype maximumGpuVertexBytes = 256 * 1024 * 1024;
constexpr int majorGridInterval = 5;
constexpr int maximumGridLines = 512;

struct Vertex final {
    float x{0.0F};
    float y{0.0F};
    float red{0.0F};
    float green{0.0F};
    float blue{0.0F};
    float alpha{1.0F};
};

[[nodiscard]] QShader loadShader(const QString& resourceName) {
    QFile file{resourceName};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

[[nodiscard]] QPointF
sceneToPixel(const QPointF& point, const ViewportState& viewport, const QSize& pixelSize) {
    return {
        ((point.x() - viewport.center.x()) * viewport.zoom) + (pixelSize.width() / 2.0),
        ((point.y() - viewport.center.y()) * viewport.zoom) + (pixelSize.height() / 2.0),
    };
}

[[nodiscard]] Vertex vertexAt(const QPointF& pixel, const QSize& pixelSize, const QColor& color) {
    return {
        static_cast<float>((pixel.x() / pixelSize.width()) * 2.0 - 1.0),
        static_cast<float>(1.0 - (pixel.y() / pixelSize.height()) * 2.0),
        color.redF(),
        color.greenF(),
        color.blueF(),
        color.alphaF(),
    };
}

void appendTriangle(std::vector<Vertex>& vertices,
                    const QPointF& first,
                    const QPointF& second,
                    const QPointF& third,
                    const QSize& pixelSize,
                    const QColor& color) {
    vertices.push_back(vertexAt(first, pixelSize, color));
    vertices.push_back(vertexAt(second, pixelSize, color));
    vertices.push_back(vertexAt(third, pixelSize, color));
}

void appendRectangle(std::vector<Vertex>& vertices,
                     const QRectF& pixelBounds,
                     const QSize& pixelSize,
                     const QColor& color) {
    const QPointF topLeft = pixelBounds.topLeft();
    const QPointF topRight = pixelBounds.topRight();
    const QPointF bottomLeft = pixelBounds.bottomLeft();
    const QPointF bottomRight = pixelBounds.bottomRight();
    appendTriangle(vertices, topLeft, bottomLeft, topRight, pixelSize, color);
    appendTriangle(vertices, topRight, bottomLeft, bottomRight, pixelSize, color);
}

void appendLine(std::vector<Vertex>& vertices,
                const QPointF& sceneStart,
                const QPointF& sceneEnd,
                const qreal width,
                const QColor& color,
                const ViewportState& viewport,
                const QSize& pixelSize) {
    const QPointF start = sceneToPixel(sceneStart, viewport, pixelSize);
    const QPointF end = sceneToPixel(sceneEnd, viewport, pixelSize);
    const QPointF delta = end - start;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (!std::isfinite(length) || length <= 0.0001) {
        return;
    }
    const qreal halfWidth = std::max<qreal>(0.5, (width * viewport.zoom) / 2.0);
    const QPointF normal{-delta.y() * halfWidth / length, delta.x() * halfWidth / length};
    appendTriangle(vertices, start + normal, start - normal, end + normal, pixelSize, color);
    appendTriangle(vertices, end + normal, start - normal, end - normal, pixelSize, color);
}

void appendRectOutline(std::vector<Vertex>& vertices,
                       const QRectF& bounds,
                       const qreal width,
                       const QColor& color,
                       const ViewportState& viewport,
                       const QSize& pixelSize) {
    appendLine(vertices, bounds.topLeft(), bounds.topRight(), width, color, viewport, pixelSize);
    appendLine(
        vertices, bounds.topRight(), bounds.bottomRight(), width, color, viewport, pixelSize);
    appendLine(
        vertices, bounds.bottomRight(), bounds.bottomLeft(), width, color, viewport, pixelSize);
    appendLine(vertices, bounds.bottomLeft(), bounds.topLeft(), width, color, viewport, pixelSize);
}

void appendBuffer(std::vector<Vertex>& vertices,
                  const SegmentBuffer& buffer,
                  const RetainedScene& scene,
                  const ViewportState& viewport,
                  const QSize& pixelSize,
                  const QTransform& transform = {}) {
    for (qsizetype index = 0; index < buffer.size(); ++index) {
        const auto& style = scene.styles()[buffer.styleIndices[index]];
        const QPointF start =
            transform.isIdentity() ? buffer.starts[index] : transform.map(buffer.starts[index]);
        const QPointF end =
            transform.isIdentity() ? buffer.ends[index] : transform.map(buffer.ends[index]);
        appendLine(vertices, start, end, style.strokeWidth, style.stroke, viewport, pixelSize);
    }
}

[[nodiscard]] std::vector<Vertex> buildVertices(const RetainedScene& scene,
                                                const ViewportState& viewport,
                                                const QSize& pixelSize,
                                                const RenderPalette& palette) {
    std::vector<Vertex> vertices;
    if (!scene.isValid() || !viewport.isValid() || pixelSize.isEmpty()) {
        return vertices;
    }
    const QRectF visible = viewport.visibleSceneRect(pixelSize);
    if (scene.page().has_value() && scene.page()->bounds.intersects(visible)) {
        const QPointF topLeft = sceneToPixel(scene.page()->bounds.topLeft(), viewport, pixelSize);
        const QPointF bottomRight =
            sceneToPixel(scene.page()->bounds.bottomRight(), viewport, pixelSize);
        appendRectangle(
            vertices, QRectF{topLeft, bottomRight}.normalized(), pixelSize, scene.page()->paper);
        appendRectOutline(vertices,
                          scene.page()->bounds,
                          1.0 / viewport.zoom,
                          palette.gridMajor,
                          viewport,
                          pixelSize);
    }
    qreal spacing = palette.gridSpacing;
    while (spacing * viewport.zoom < 12.0) {
        spacing *= majorGridInterval;
    }
    int count = palette.gridVisible ? 0 : maximumGridLines;
    for (qreal x = std::floor(visible.left() / spacing) * spacing;
         x <= visible.right() && count < maximumGridLines;
         x += spacing, ++count) {
        const auto ordinal = static_cast<qint64>(std::llround(x / spacing));
        appendLine(vertices,
                   {x, visible.top()},
                   {x, visible.bottom()},
                   1.0 / viewport.zoom,
                   (ordinal % majorGridInterval) == 0 ? palette.gridMajor : palette.gridMinor,
                   viewport,
                   pixelSize);
    }
    count = palette.gridVisible ? 0 : maximumGridLines;
    for (qreal y = std::floor(visible.top() / spacing) * spacing;
         y <= visible.bottom() && count < maximumGridLines;
         y += spacing, ++count) {
        const auto ordinal = static_cast<qint64>(std::llround(y / spacing));
        appendLine(vertices,
                   {visible.left(), y},
                   {visible.right(), y},
                   1.0 / viewport.zoom,
                   (ordinal % majorGridInterval) == 0 ? palette.gridMajor : palette.gridMinor,
                   viewport,
                   pixelSize);
    }
    const auto candidates = scene.spatialIndex().query(visible);
    for (const auto& record : candidates) {
        if (record.kind == SceneItemKind::Segment) {
            const auto& buffer = scene.segments();
            const auto& style = scene.styles()[buffer.styleIndices[record.index]];
            appendLine(vertices,
                       buffer.starts[record.index],
                       buffer.ends[record.index],
                       style.strokeWidth,
                       style.stroke,
                       viewport,
                       pixelSize);
        } else if (record.kind == SceneItemKind::Symbol) {
            const auto& instance = scene.symbolInstances()[record.index];
            const auto& geometry = scene.symbolGeometries()[instance.geometryIndex];
            appendBuffer(vertices,
                         canvas::symbolSegments(
                             geometry, canvas::selectSymbolLevelOfDetail(instance, viewport.zoom)),
                         scene,
                         viewport,
                         pixelSize,
                         instance.transform);
        }
    }
    return vertices;
}

class SurfaceBackend {
  public:
    virtual ~SurfaceBackend() = default;
    [[nodiscard]] virtual QWidget* widget() noexcept = 0;
    virtual void setScene(std::shared_ptr<const RetainedScene> scene) = 0;
    virtual void setViewport(const ViewportState& viewport) = 0;
    virtual void setPalette(const RenderPalette& palette) = 0;
    [[nodiscard]] virtual canvas::RenderStatistics lastRender() const noexcept = 0;
    [[nodiscard]] virtual qsizetype gpuVertexBytes() const noexcept = 0;
};

class SoftwareSurface final : public QWidget, public SurfaceBackend {
  public:
    explicit SoftwareSurface(std::function<void()> rendered, QWidget* parent = nullptr)
        : QWidget{parent}, rendered_{std::move(rendered)} {
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    QWidget* widget() noexcept override {
        return this;
    }
    void setScene(std::shared_ptr<const RetainedScene> scene) override {
        if (scene_ == scene) {
            return;
        }
        scene_ = std::move(scene);
        update();
    }
    void setViewport(const ViewportState& viewport) override {
        viewport_ = viewport;
        update();
    }
    void setPalette(const RenderPalette& palette) override {
        palette_ = palette;
        update();
    }
    canvas::RenderStatistics lastRender() const noexcept override {
        return lastRender_;
    }
    qsizetype gpuVertexBytes() const noexcept override {
        return 0;
    }

  protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event)
        QPainter painter{this};
        painter.fillRect(rect(), palette_.canvas);
        lastRender_ = {};
        if (scene_ != nullptr) {
            lastRender_ = renderer_.render(painter, *scene_, viewport_, size(), palette_);
        }
        rendered_();
    }

  private:
    std::shared_ptr<const RetainedScene> scene_;
    ViewportState viewport_;
    RenderPalette palette_;
    canvas::PainterSceneRenderer renderer_;
    canvas::RenderStatistics lastRender_;
    std::function<void()> rendered_;
};

class TextOverlay final : public QWidget {
  public:
    explicit TextOverlay(QWidget* parent = nullptr) : QWidget{parent} {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    void setScene(std::shared_ptr<const RetainedScene> scene) {
        scene_ = std::move(scene);
        update();
    }
    void setViewport(const ViewportState& viewport) {
        viewport_ = viewport;
        update();
    }
    void setPalette(const RenderPalette& palette) {
        palette_ = palette;
        update();
    }
    [[nodiscard]] canvas::RenderStatistics lastRender() const noexcept {
        return lastRender_;
    }

  protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event)
        lastRender_ = {};
        if (scene_ == nullptr) {
            return;
        }
        QPainter painter{this};
        lastRender_ = renderer_.render(
            painter, *scene_, viewport_, size(), palette_, PainterRenderPass::TextAndDiagnostics);
    }

  private:
    std::shared_ptr<const RetainedScene> scene_;
    ViewportState viewport_;
    RenderPalette palette_;
    canvas::PainterSceneRenderer renderer_;
    canvas::RenderStatistics lastRender_;
};

class RhiGeometryWidget final : public QRhiWidget {
  public:
    RhiGeometryWidget(std::function<void()> rendered,
                      std::function<void()> failed,
                      QWidget* parent = nullptr)
        : QRhiWidget{parent}, rendered_{std::move(rendered)}, failed_{std::move(failed)} {
        setSampleCount(4);
    }

    void setScene(std::shared_ptr<const RetainedScene> scene) {
        if (scene_ == scene) {
            return;
        }
        scene_ = std::move(scene);
        dirty_ = true;
        update();
    }
    void setViewport(const ViewportState& viewport) {
        viewport_ = viewport;
        dirty_ = true;
        update();
    }
    void setPalette(const RenderPalette& palette) {
        palette_ = palette;
        dirty_ = true;
        update();
    }
    [[nodiscard]] qsizetype vertexBytes() const noexcept {
        return vertexBytes_;
    }

  protected:
    void initialize(QRhiCommandBuffer* commandBuffer) override {
        Q_UNUSED(commandBuffer)
        if (rhi_ != rhi()) {
            releaseResources();
            rhi_ = rhi();
        }
        if (pipeline_ != nullptr || rhi_ == nullptr) {
            return;
        }
        bindings_.reset(rhi_->newShaderResourceBindings());
        if (!bindings_->create()) {
            requestFailure();
            return;
        }
        pipeline_.reset(rhi_->newGraphicsPipeline());
        const QShader vertexShader =
            loadShader(QStringLiteral(":/aimora/renderer/shaders/scene.vert.qsb"));
        const QShader fragmentShader =
            loadShader(QStringLiteral(":/aimora/renderer/shaders/scene.frag.qsb"));
        if (!vertexShader.isValid() || !fragmentShader.isValid()) {
            requestFailure();
            return;
        }
        pipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, vertexShader},
            {QRhiShaderStage::Fragment, fragmentShader},
        });
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{sizeof(Vertex)}});
        inputLayout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float2, offsetof(Vertex, x)},
            {0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, red)},
        });
        pipeline_->setVertexInputLayout(inputLayout);
        pipeline_->setShaderResourceBindings(bindings_.get());
        pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipeline_->setTargetBlends({blend});
        pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        if (!pipeline_->create()) {
            requestFailure();
        }
    }

    void render(QRhiCommandBuffer* commandBuffer) override {
        if (rhi_ == nullptr || pipeline_ == nullptr) {
            return;
        }
        QRhiResourceUpdateBatch* updates = nullptr;
        if (dirty_) {
            vertices_ = scene_ == nullptr ? std::vector<Vertex>{}
                                          : buildVertices(*scene_, viewport_, size(), palette_);
            vertexBytes_ = static_cast<qsizetype>(vertices_.size() * sizeof(Vertex));
            if (vertexBytes_ > maximumGpuVertexBytes) {
                requestFailure();
                return;
            }
            const bool shouldResizeBuffer =
                vertexBytes_ > 0 &&
                (vertexBuffer_ == nullptr || vertexCapacityBytes_ < vertexBytes_ ||
                 vertexCapacityBytes_ > std::max<qsizetype>(64 * 1024, vertexBytes_ * 4));
            if (vertexBytes_ == 0) {
                vertexBuffer_.reset();
                vertexCapacityBytes_ = 0;
            } else if (shouldResizeBuffer) {
                vertexBuffer_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic,
                                                    QRhiBuffer::VertexBuffer,
                                                    static_cast<quint32>(vertexBytes_)));
                if (!vertexBuffer_->create()) {
                    requestFailure();
                    return;
                }
                vertexCapacityBytes_ = vertexBytes_;
            }
            if (vertexBytes_ > 0) {
                updates = rhi_->nextResourceUpdateBatch();
                updates->updateDynamicBuffer(
                    vertexBuffer_.get(), 0, static_cast<quint32>(vertexBytes_), vertices_.data());
            }
            dirty_ = false;
        }
        commandBuffer->beginPass(renderTarget(), palette_.canvas, {1.0F, 0}, updates);
        if (vertexBytes_ > 0 && vertexBuffer_ != nullptr) {
            commandBuffer->setGraphicsPipeline(pipeline_.get());
            const QSize outputSize = colorTexture()->pixelSize();
            commandBuffer->setViewport(QRhiViewport{0.0F,
                                                    0.0F,
                                                    static_cast<float>(outputSize.width()),
                                                    static_cast<float>(outputSize.height())});
            commandBuffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput binding{vertexBuffer_.get(), 0};
            commandBuffer->setVertexInput(0, 1, &binding);
            commandBuffer->draw(static_cast<quint32>(vertices_.size()));
        }
        commandBuffer->endPass();
        rendered_();
    }

    void releaseResources() override {
        vertexBuffer_.reset();
        pipeline_.reset();
        bindings_.reset();
        rhi_ = nullptr;
        vertexBytes_ = 0;
        vertexCapacityBytes_ = 0;
        vertices_.clear();
        dirty_ = true;
    }

    void resizeEvent(QResizeEvent* event) override {
        dirty_ = true;
        QRhiWidget::resizeEvent(event);
    }

  private:
    void requestFailure() {
        if (failureRequested_) {
            return;
        }
        failureRequested_ = true;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [callback = failed_]() { callback(); },
            Qt::QueuedConnection);
    }

    std::shared_ptr<const RetainedScene> scene_;
    ViewportState viewport_;
    RenderPalette palette_;
    QRhi* rhi_{nullptr};
    std::unique_ptr<QRhiBuffer> vertexBuffer_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    std::vector<Vertex> vertices_;
    qsizetype vertexBytes_{0};
    qsizetype vertexCapacityBytes_{0};
    bool dirty_{true};
    bool failureRequested_{false};
    std::function<void()> rendered_;
    std::function<void()> failed_;
};

class RhiCompositeSurface final : public QWidget, public SurfaceBackend {
  public:
    RhiCompositeSurface(std::function<void()> rendered,
                        std::function<void()> failed,
                        QWidget* parent = nullptr)
        : QWidget{parent} {
        auto* layout = new QStackedLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setStackingMode(QStackedLayout::StackAll);
        geometry_ = new RhiGeometryWidget{std::move(rendered), std::move(failed), this};
        overlay_ = new TextOverlay{this};
        layout->addWidget(geometry_);
        layout->addWidget(overlay_);
        layout->setCurrentWidget(overlay_);
    }
    QWidget* widget() noexcept override {
        return this;
    }
    void setScene(std::shared_ptr<const RetainedScene> scene) override {
        geometry_->setScene(scene);
        overlay_->setScene(std::move(scene));
    }
    void setViewport(const ViewportState& viewport) override {
        geometry_->setViewport(viewport);
        overlay_->setViewport(viewport);
    }
    void setPalette(const RenderPalette& palette) override {
        geometry_->setPalette(palette);
        overlay_->setPalette(palette);
    }
    canvas::RenderStatistics lastRender() const noexcept override {
        return overlay_->lastRender();
    }
    qsizetype gpuVertexBytes() const noexcept override {
        return geometry_->vertexBytes();
    }

  private:
    RhiGeometryWidget* geometry_{nullptr};
    TextOverlay* overlay_{nullptr};
};

[[nodiscard]] RendererBackend resolveBackend(const RendererBackend requested) {
    const QByteArray override = qgetenv("AIMORA_RENDERER").toLower();
    if (override == "software" || QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        return RendererBackend::SoftwarePainter;
    }
    if (override == "rhi") {
        return RendererBackend::AcceleratedRhi;
    }
    return requested == RendererBackend::SoftwarePainter ? RendererBackend::SoftwarePainter
                                                         : RendererBackend::AcceleratedRhi;
}

} // namespace

class SceneSurface::Private final {
  public:
    explicit Private(SceneSurface& owner, const RendererBackend requested)
        : owner_{owner}, backendType_{resolveBackend(requested)} {
        auto* layout = new QVBoxLayout{&owner_};
        layout->setContentsMargins(0, 0, 0, 0);
        createBackend();
    }

    void createBackend() {
        if (backend_ != nullptr) {
            QWidget* oldWidget = backend_->widget();
            owner_.layout()->removeWidget(oldWidget);
            backend_.reset();
        }
        const auto rendered = [this]() {
            ++statistics_.renderedFrames;
            statistics_.lastRender = backend_->lastRender();
            statistics_.gpuVertexBytes = backend_->gpuVertexBytes();
        };
        if (backendType_ == RendererBackend::AcceleratedRhi) {
            backend_ = std::make_unique<RhiCompositeSurface>(
                rendered,
                [this]() {
                    backendType_ = RendererBackend::SoftwarePainter;
                    statistics_.backend = backendType_;
                    createBackend();
                    applyState();
                },
                &owner_);
        } else {
            backend_ = std::make_unique<SoftwareSurface>(rendered, &owner_);
        }
        statistics_.backend = backendType_;
        owner_.layout()->addWidget(backend_->widget());
        applyState();
    }

    void applyState() {
        backend_->setScene(scene_);
        backend_->setViewport(viewport_);
        backend_->setPalette(palette_);
        ++statistics_.requestedFrames;
    }

    SceneSurface& owner_;
    RendererBackend backendType_{RendererBackend::SoftwarePainter};
    std::unique_ptr<SurfaceBackend> backend_;
    std::shared_ptr<const RetainedScene> scene_;
    ViewportState viewport_;
    RenderPalette palette_;
    SurfaceStatistics statistics_;
};

SceneSurface::SceneSurface(const RendererBackend requestedBackend, QWidget* parent)
    : QWidget{parent}, private_{std::make_unique<Private>(*this, requestedBackend)} {
    setObjectName(QStringLiteral("aimoraSceneSurface"));
    setAccessibleName(tr("Engineering drawing scene"));
    setAccessibleDescription(tr("Retained vector scene with software fallback."));
    setFocusPolicy(Qt::StrongFocus);
}

SceneSurface::~SceneSurface() = default;

void SceneSurface::setScene(std::shared_ptr<const RetainedScene> scene) {
    if (private_->scene_ == scene) {
        return;
    }
    private_->scene_ = std::move(scene);
    private_->statistics_.sceneGeneration =
        private_->scene_ == nullptr ? 0 : private_->scene_->generation();
    private_->statistics_.cpuSceneBytes =
        private_->scene_ == nullptr ? 0 : private_->scene_->statistics().estimatedBytes;
    private_->backend_->setScene(private_->scene_);
    ++private_->statistics_.requestedFrames;
}

std::shared_ptr<const RetainedScene> SceneSurface::scene() const noexcept {
    return private_->scene_;
}

void SceneSurface::setViewport(const ViewportState& viewport) {
    if (!viewport.isValid()) {
        return;
    }
    if (private_->viewport_.center == viewport.center &&
        private_->viewport_.zoom == viewport.zoom) {
        return;
    }
    private_->viewport_ = viewport;
    private_->backend_->setViewport(viewport);
    ++private_->statistics_.requestedFrames;
}

const ViewportState& SceneSurface::viewport() const noexcept {
    return private_->viewport_;
}

void SceneSurface::setRenderPalette(const RenderPalette& palette) {
    if (!palette.isValid()) {
        return;
    }
    private_->palette_ = palette;
    private_->backend_->setPalette(palette);
    ++private_->statistics_.requestedFrames;
}

const RenderPalette& SceneSurface::renderPalette() const noexcept {
    return private_->palette_;
}
RendererBackend SceneSurface::backend() const noexcept {
    return private_->backendType_;
}
const SurfaceStatistics& SceneSurface::statistics() const noexcept {
    return private_->statistics_;
}

QVector<canvas::SceneItemId> SceneSurface::hitTest(const QPointF& pixelPoint,
                                                   const qreal tolerancePixels) const {
    if (private_->scene_ == nullptr || !std::isfinite(tolerancePixels) || tolerancePixels < 0.0 ||
        private_->viewport_.zoom <= 0.0) {
        return {};
    }
    const QPointF scenePoint{
        private_->viewport_.center.x() +
            ((pixelPoint.x() - (width() / 2.0)) / private_->viewport_.zoom),
        private_->viewport_.center.y() +
            ((pixelPoint.y() - (height() / 2.0)) / private_->viewport_.zoom),
    };
    return private_->scene_->spatialIndex().hitTest(scenePoint,
                                                    tolerancePixels / private_->viewport_.zoom);
}

QImage SceneSurface::renderReferenceImage(const QSize& pixelSize,
                                          const qreal devicePixelRatio) const {
    if (private_->scene_ == nullptr) {
        return {};
    }
    return canvas::PainterSceneRenderer{}.renderReferenceImage(
        *private_->scene_, private_->viewport_, pixelSize, private_->palette_, devicePixelRatio);
}

} // namespace aimora::studio::renderer
