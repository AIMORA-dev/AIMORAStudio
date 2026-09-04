// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QStackedLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace aimora::studio::shell {
namespace {

constexpr qsizetype maximumNearbyRecords = 128;
constexpr qsizetype maximumPortCandidates = 1'024;
constexpr qreal minimumMarqueePixels = 3.0;
constexpr qreal crosshairRadiusPixels = 10.0;
constexpr qreal gripRadiusPixels = 4.0;

class DrawingInteractionSurface final : public QWidget {
  public:
    using PaintHandler = std::function<void(QPainter& painter, const QSizeF& pixelExtent)>;

    DrawingInteractionSurface(PaintHandler paintHandler, QWidget* parent)
        : QWidget{parent}, paintHandler_{std::move(paintHandler)} {
        setObjectName(QStringLiteral("aimoraDrawingInteractionSurface"));
        setAccessibleName(tr("Precision drawing interaction surface"));
        setAccessibleDescription(
            tr("Crosshair, selection, snapping, grips, guides, and command previews."));
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAcceptDrops(true);
    }

  protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        paintHandler_(painter, size());
    }

  private:
    PaintHandler paintHandler_;
};

[[nodiscard]] commands::SelectionOperation
selectionOperation(const Qt::KeyboardModifiers modifiers) noexcept {
    if (modifiers.testFlag(Qt::AltModifier)) {
        return commands::SelectionOperation::Subtract;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        return commands::SelectionOperation::Toggle;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        return commands::SelectionOperation::Add;
    }
    return commands::SelectionOperation::Replace;
}

[[nodiscard]] QVector<commands::SelectionRecord>
selectionRecords(const QVector<canvas::SpatialRecord>& spatialRecords) {
    QVector<commands::SelectionRecord> records;
    records.reserve(spatialRecords.size());
    for (const canvas::SpatialRecord& record : spatialRecords) {
        if (record.kind == canvas::SceneItemKind::Page ||
            record.kind == canvas::SceneItemKind::Overlay) {
            continue;
        }
        records.append({record.id, record.bounds});
    }
    return records;
}

void appendSegmentCandidates(QVector<commands::SnapCandidate>& candidates,
                             const canvas::RetainedScene& scene,
                             const canvas::SpatialRecord& record) {
    const canvas::SegmentBuffer& segments = scene.segments();
    if (record.index < 0 || record.index >= segments.size()) {
        return;
    }
    const QPointF start = segments.starts[record.index];
    const QPointF end = segments.ends[record.index];
    candidates.append({record.id, start, commands::SnapKind::Endpoint, {}});
    candidates.append({record.id, end, commands::SnapKind::Endpoint, {}});
    candidates.append({record.id, (start + end) / 2.0, commands::SnapKind::Midpoint, {}});
}

void appendBoundsCandidates(QVector<commands::SnapCandidate>& candidates,
                            const canvas::SpatialRecord& record) {
    const QRectF bounds = record.bounds.normalized();
    candidates.append({record.id, bounds.center(), commands::SnapKind::Center, {}});
    candidates.append({record.id, bounds.topLeft(), commands::SnapKind::Endpoint, {}});
    candidates.append({record.id, bounds.topRight(), commands::SnapKind::Endpoint, {}});
    candidates.append({record.id, bounds.bottomLeft(), commands::SnapKind::Endpoint, {}});
    candidates.append({record.id, bounds.bottomRight(), commands::SnapKind::Endpoint, {}});
}

void appendIntersections(QVector<commands::SnapCandidate>& candidates,
                         const canvas::RetainedScene& scene,
                         const QVector<canvas::SpatialRecord>& records) {
    const canvas::SegmentBuffer& segments = scene.segments();
    for (qsizetype first = 0; first < records.size(); ++first) {
        if (records[first].kind != canvas::SceneItemKind::Segment || records[first].index < 0 ||
            records[first].index >= segments.size()) {
            continue;
        }
        const QLineF firstLine{segments.starts[records[first].index],
                               segments.ends[records[first].index]};
        for (qsizetype second = first + 1; second < records.size(); ++second) {
            if (records[second].kind != canvas::SceneItemKind::Segment ||
                records[second].index < 0 || records[second].index >= segments.size()) {
                continue;
            }
            const QLineF secondLine{segments.starts[records[second].index],
                                    segments.ends[records[second].index]};
            QPointF intersection;
            if (firstLine.intersects(secondLine, &intersection) == QLineF::BoundedIntersection) {
                candidates.append({std::min(records[first].id, records[second].id),
                                   intersection,
                                   commands::SnapKind::Intersection,
                                   {}});
            }
        }
    }
}

[[nodiscard]] QVector<commands::SnapCandidate>
nearbySnapCandidates(const std::shared_ptr<const canvas::RetainedScene>& scene,
                     const QPointF& scenePoint,
                     const qreal toleranceScene,
                     const QVector<commands::SnapCandidate>& electricalPorts) {
    QVector<commands::SnapCandidate> candidates;
    if (scene == nullptr || toleranceScene < 0.0 || !std::isfinite(toleranceScene)) {
        return candidates;
    }
    const QRectF queryArea{scenePoint - QPointF{toleranceScene, toleranceScene},
                           QSizeF{toleranceScene * 2.0, toleranceScene * 2.0}};
    QVector<canvas::SpatialRecord> records = scene->spatialIndex().query(queryArea);
    if (records.size() > maximumNearbyRecords) {
        records.resize(maximumNearbyRecords);
    }
    candidates.reserve(records.size() * 5 +
                       std::min(electricalPorts.size(), maximumPortCandidates));
    for (const canvas::SpatialRecord& record : records) {
        if (record.kind == canvas::SceneItemKind::Segment) {
            appendSegmentCandidates(candidates, *scene, record);
        } else if (record.kind == canvas::SceneItemKind::Symbol ||
                   record.kind == canvas::SceneItemKind::Text) {
            appendBoundsCandidates(candidates, record);
        }
    }
    appendIntersections(candidates, *scene, records);
    const qsizetype portCount = std::min(electricalPorts.size(), maximumPortCandidates);
    for (qsizetype index = 0; index < portCount; ++index) {
        candidates.append(electricalPorts[index]);
    }
    return candidates;
}

[[nodiscard]] QVector<commands::SelectionRecord>
visibleSelectionRecords(const std::shared_ptr<const canvas::RetainedScene>& scene,
                        const commands::PrecisionViewport& viewport,
                        const QSizeF& pixelExtent) {
    if (scene == nullptr || !viewport.isValid(pixelExtent)) {
        return {};
    }
    const QPointF topLeft = viewport.scenePoint(QPointF{0.0, 0.0}, pixelExtent);
    const QPointF bottomRight =
        viewport.scenePoint(QPointF{pixelExtent.width(), pixelExtent.height()}, pixelExtent);
    return selectionRecords(scene->spatialIndex().query(QRectF{topLeft, bottomRight}.normalized()));
}

} // namespace

DrawingWorkspace::DrawingWorkspace(QWidget* parent) : QWidget{parent} {
    setObjectName(QStringLiteral("aimoraDrawingWorkspace"));
    setAccessibleName(tr("AIMORA drawing workspace"));
    setAccessibleDescription(
        tr("Central engineering drawing area for single-line diagrams and CAD sheets."));
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QStackedLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setStackingMode(QStackedLayout::StackAll);
    sceneSurface_ = new renderer::SceneSurface{renderer::RendererBackend::Automatic, this};
    sceneSurface_->setObjectName(QStringLiteral("aimoraSceneSurface"));
    sceneSurface_->setAccessibleName(tr("Engineering drawing canvas"));
    sceneSurface_->setAccessibleDescription(
        tr("Retained-scene view of the current single-line diagram or CAD sheet."));
    layout->addWidget(sceneSurface_);

    auto paintInteraction = [this](QPainter& painter, const QSizeF& pixelExtent) {
        if (marqueeActive_) {
            const QPointF first =
                precisionViewport_.pixelPoint(marqueeSceneArea_.topLeft(), pixelExtent);
            const QPointF second =
                precisionViewport_.pixelPoint(marqueeSceneArea_.bottomRight(), pixelExtent);
            QColor fill = tokens_.selection;
            fill.setAlpha(38);
            painter.setPen(QPen{tokens_.selection, 1.0, Qt::DashLine});
            painter.setBrush(fill);
            painter.drawRect(QRectF{first, second}.normalized());
        }

        painter.setBrush(Qt::NoBrush);
        QPen guidePen{tokens_.selection, 1.0, Qt::DashLine};
        guidePen.setCosmetic(true);
        painter.setPen(guidePen);
        for (const commands::AlignmentGuide& guide : pointerSnap_.guides) {
            if (guide.orientation == Qt::Vertical) {
                const qreal x =
                    precisionViewport_.pixelPoint(QPointF{guide.coordinate, 0.0}, pixelExtent).x();
                painter.drawLine(QPointF{x, 0.0}, QPointF{x, pixelExtent.height()});
            } else {
                const qreal y =
                    precisionViewport_.pixelPoint(QPointF{0.0, guide.coordinate}, pixelExtent).y();
                painter.drawLine(QPointF{0.0, y}, QPointF{pixelExtent.width(), y});
            }
        }

        const commands::CommandPreview preview = commandSession_.preview();
        QPen previewPen{tokens_.selection, 1.5, Qt::DashLine};
        previewPen.setCosmetic(true);
        painter.setPen(previewPen);
        for (const QLineF& segment : preview.segments) {
            painter.drawLine(QLineF{precisionViewport_.pixelPoint(segment.p1(), pixelExtent),
                                    precisionViewport_.pixelPoint(segment.p2(), pixelExtent)});
        }

        const auto records =
            visibleSelectionRecords(sceneSurface_->scene(), precisionViewport_, pixelExtent);
        const auto handles = selection_.handles(records);
        for (const commands::EditHandle& handle : handles) {
            const QPointF pixel = precisionViewport_.pixelPoint(handle.point, pixelExtent);
            painter.setPen(QPen{tokens_.selection, 1.0});
            painter.setBrush(handle.kind == commands::EditHandleKind::Grip ? tokens_.canvas
                                                                           : tokens_.selection);
            if (handle.kind == commands::EditHandleKind::Grip) {
                painter.drawRect(QRectF{pixel - QPointF{gripRadiusPixels, gripRadiusPixels},
                                        QSizeF{gripRadiusPixels * 2.0, gripRadiusPixels * 2.0}});
            } else {
                const QPolygonF diamond{{pixel.x(), pixel.y() - gripRadiusPixels},
                                        {pixel.x() + gripRadiusPixels, pixel.y()},
                                        {pixel.x(), pixel.y() + gripRadiusPixels},
                                        {pixel.x() - gripRadiusPixels, pixel.y()}};
                painter.drawPolygon(diamond);
            }
        }

        if (pointerAvailable_) {
            const QPointF pointer =
                precisionViewport_.pixelPoint(pointerSnap_.scenePoint, pixelExtent);
            QPen crosshairPen{tokens_.textPrimary, 1.0};
            crosshairPen.setCosmetic(true);
            painter.setPen(crosshairPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(pointer - QPointF{crosshairRadiusPixels, 0.0},
                             pointer + QPointF{crosshairRadiusPixels, 0.0});
            painter.drawLine(pointer - QPointF{0.0, crosshairRadiusPixels},
                             pointer + QPointF{0.0, crosshairRadiusPixels});
            if (pointerSnap_.snapped()) {
                painter.setPen(QPen{tokens_.selection, 1.5});
                painter.drawEllipse(pointer, gripRadiusPixels + 1.0, gripRadiusPixels + 1.0);
            }
        }
    };
    interactionSurface_ = new DrawingInteractionSurface{std::move(paintInteraction), this};
    interactionSurface_->installEventFilter(this);
    layout->addWidget(interactionSurface_);
    interactionSurface_->raise();
    setFocusProxy(interactionSurface_);
    setThemeTokens(tokens_);
}

void DrawingWorkspace::setScene(std::shared_ptr<const canvas::RetainedScene> scene) {
    sceneSurface_->setScene(std::move(scene));
    selection_.clear();
    if (inspectionSelectionHandler_) {
        inspectionSelectionHandler_({}, false);
    }
    interactionSurface_->update();
}

renderer::SceneSurface* DrawingWorkspace::sceneSurface() const noexcept {
    return sceneSurface_;
}

void DrawingWorkspace::setThemeTokens(const themes::ThemeTokens& tokens) {
    if (!tokens.isValid()) {
        return;
    }
    tokens_ = tokens;
    QPalette workspacePalette = palette();
    workspacePalette.setColor(QPalette::Window, tokens_.canvas);
    workspacePalette.setColor(QPalette::WindowText, tokens_.textPrimary);
    setPalette(workspacePalette);
    sceneSurface_->setRenderPalette(canvas::RenderPalette{
        .canvas = tokens_.canvas,
        .gridMinor = tokens_.gridMinor,
        .gridMajor = tokens_.gridMajor,
        .selection = tokens_.selection,
        .diagnostic = tokens_.error,
        .text = tokens_.textPrimary,
    });
    interactionSurface_->update();
}

const themes::ThemeTokens& DrawingWorkspace::themeTokens() const noexcept {
    return tokens_;
}

void DrawingWorkspace::setElectricalPortSnaps(QVector<commands::SnapCandidate> ports) {
    ports.erase(std::remove_if(ports.begin(),
                               ports.end(),
                               [](const commands::SnapCandidate& candidate) {
                                   return candidate.kind != commands::SnapKind::ElectricalPort ||
                                          candidate.semanticId.trimmed().isEmpty() ||
                                          !std::isfinite(candidate.point.x()) ||
                                          !std::isfinite(candidate.point.y());
                               }),
                ports.end());
    if (ports.size() > maximumPortCandidates) {
        ports.resize(maximumPortCandidates);
    }
    electricalPortSnaps_ = std::move(ports);
}

void DrawingWorkspace::setSemanticItemIds(QHash<quint64, QString> semanticItemIds) {
    for (auto iterator = semanticItemIds.begin(); iterator != semanticItemIds.end();) {
        if (iterator.key() == 0 || iterator.value().trimmed().isEmpty()) {
            iterator = semanticItemIds.erase(iterator);
        } else {
            ++iterator;
        }
    }
    semanticItemIds_ = std::move(semanticItemIds);
}

void DrawingWorkspace::setCanonicalEditHandler(CanonicalEditHandler handler) {
    canonicalEditHandler_ = std::move(handler);
}

void DrawingWorkspace::setInspectionSelectionHandler(InspectionSelectionHandler handler) {
    inspectionSelectionHandler_ = std::move(handler);
}

bool DrawingWorkspace::executeCommandText(QStringView input) {
    const QString normalized = input.toString().trimmed().toLower();
    if (normalized.isEmpty()) {
        if (!commandSession_.isActive()) {
            return false;
        }
        completeCommand();
        return true;
    }
    if (normalized == QStringLiteral("grid")) {
        setGridSnapEnabled(!gridSnapEnabled());
        return true;
    }
    if (normalized == QStringLiteral("ortho")) {
        setOrthoEnabled(!orthoEnabled());
        return true;
    }
    if (normalized == QStringLiteral("polar")) {
        setPolarEnabled(!polarEnabled());
        return true;
    }
    if (normalized == QStringLiteral("snap")) {
        snapSettings_.objectEnabled = !snapSettings_.objectEnabled;
        interactionSurface_->update();
        return true;
    }
    if (normalized == QStringLiteral("select")) {
        commandSession_.cancel();
        interactionSurface_->update();
        return true;
    }
    if (normalized == QStringLiteral("pan") || normalized == QStringLiteral("zoom")) {
        commandSession_.cancel();
        interactionSurface_->setFocus();
        return true;
    }
    if (commandSession_.isActive()) {
        const QPointF anchor = commandSession_.anchor().value_or(pointerSnap_.scenePoint);
        const auto coordinate = commands::CoordinateInterpreter::parse(normalized, anchor);
        if (!coordinate.has_value()) {
            return false;
        }
        const bool accepted = commandSession_.acceptPoint(coordinate->point);
        interactionSurface_->update();
        return accepted;
    }
    if (!canonicalEditHandler_) {
        return false;
    }
    const auto started = commandSession_.begin(normalized);
    interactionSurface_->update();
    return started == commands::CommandStartResult::Started;
}

void DrawingWorkspace::setGridSnapEnabled(const bool enabled) {
    snapSettings_.gridEnabled = enabled;
    if (pointerAvailable_) {
        updatePointer(pointerPixel_);
    }
}

void DrawingWorkspace::setOrthoEnabled(const bool enabled) {
    snapSettings_.orthoEnabled = enabled;
    if (enabled) {
        snapSettings_.polarEnabled = false;
    }
    if (pointerAvailable_) {
        updatePointer(pointerPixel_);
    }
}

void DrawingWorkspace::setPolarEnabled(const bool enabled) {
    snapSettings_.polarEnabled = enabled;
    if (enabled) {
        snapSettings_.orthoEnabled = false;
    }
    if (pointerAvailable_) {
        updatePointer(pointerPixel_);
    }
}

bool DrawingWorkspace::gridSnapEnabled() const noexcept {
    return snapSettings_.gridEnabled;
}

bool DrawingWorkspace::orthoEnabled() const noexcept {
    return snapSettings_.orthoEnabled;
}

bool DrawingWorkspace::polarEnabled() const noexcept {
    return snapSettings_.polarEnabled;
}

const commands::SelectionModel& DrawingWorkspace::selection() const noexcept {
    return selection_;
}

const commands::PrecisionViewport& DrawingWorkspace::precisionViewport() const noexcept {
    return precisionViewport_;
}

QWidget* DrawingWorkspace::interactionSurface() const noexcept {
    return interactionSurface_;
}

quint64 DrawingWorkspace::dispatchedCanonicalEditCount() const noexcept {
    return dispatchedCanonicalEditCount_;
}

QSize DrawingWorkspace::sizeHint() const {
    return QSize{1200, 760};
}

bool DrawingWorkspace::eventFilter(QObject* watched, QEvent* event) {
    if (watched != interactionSurface_) {
        return QWidget::eventFilter(watched, event);
    }
    const QSizeF pixelExtent = interactionSurface_->size();
    switch (event->type()) {
    case QEvent::DragEnter: {
        auto* dragEvent = static_cast<QDragEnterEvent*>(event);
        if (dragEvent->mimeData()->hasFormat(
                QStringLiteral("application/vnd.aimora.catalog-entry+json"))) {
            dragEvent->acceptProposedAction();
            return true;
        }
        return false;
    }
    case QEvent::Drop: {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        const QByteArray payload = dropEvent->mimeData()->data(
            QStringLiteral("application/vnd.aimora.catalog-entry+json"));
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return false;
        }
        const QJsonObject object = document.object();
        const QString catalogId = object.value(QStringLiteral("catalog_id")).toString();
        const QString kind = object.value(QStringLiteral("kind")).toString();
        const QPointF scenePoint = precisionViewport_.scenePoint(
            dropEvent->position(), QSizeF{interactionSurface_->size()});
        if (!requestEquipmentPlacement(catalogId, kind == QStringLiteral("assembly"), scenePoint)) {
            return false;
        }
        dropEvent->acceptProposedAction();
        return true;
    }
    case QEvent::MouseMove: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (panning_) {
            precisionViewport_.panBy(mouseEvent->position() - previousPanPixel_);
            previousPanPixel_ = mouseEvent->position();
            applyViewport();
        }
        updatePointer(mouseEvent->position());
        if (marqueeActive_) {
            const QPointF origin = precisionViewport_.scenePoint(dragOriginPixel_, pixelExtent);
            marqueeSceneArea_ = QRectF{origin, pointerSnap_.scenePoint};
        }
        interactionSurface_->update();
        return true;
    }
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        interactionSurface_->setFocus();
        updatePointer(mouseEvent->position());
        if (mouseEvent->button() == Qt::MiddleButton ||
            (mouseEvent->button() == Qt::LeftButton && spacePanEnabled_)) {
            panning_ = true;
            previousPanPixel_ = mouseEvent->position();
            interactionSurface_->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }
        if (commandSession_.isActive()) {
            const bool accepted = commandSession_.acceptPoint(
                pointerSnap_.scenePoint,
                pointerSnap_.kind == commands::SnapKind::ElectricalPort ? pointerSnap_.semanticId
                                                                        : QString{});
            Q_UNUSED(accepted);
            interactionSurface_->update();
            return true;
        }
        const QVector<canvas::SceneItemId> hits =
            sceneSurface_->hitTest(mouseEvent->position(), snapSettings_.tolerancePixels);
        if (!hits.isEmpty()) {
            selection_.applyHit(hits, selectionOperation(mouseEvent->modifiers()));
            if (inspectionSelectionHandler_) {
                inspectionSelectionHandler_(selection_.selectedIds(), false);
            }
            interactionSurface_->update();
            return true;
        }
        dragOriginPixel_ = mouseEvent->position();
        marqueeActive_ = true;
        marqueeSceneArea_ = QRectF{pointerSnap_.scenePoint, pointerSnap_.scenePoint};
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (panning_ &&
            (mouseEvent->button() == Qt::MiddleButton || mouseEvent->button() == Qt::LeftButton)) {
            panning_ = false;
            interactionSurface_->setCursor(Qt::CrossCursor);
            return true;
        }
        if (mouseEvent->button() != Qt::LeftButton || !marqueeActive_) {
            return false;
        }
        updatePointer(mouseEvent->position());
        const bool crossing = mouseEvent->position().x() < dragOriginPixel_.x();
        const qreal dragLength = QLineF{dragOriginPixel_, mouseEvent->position()}.length();
        const auto operation = selectionOperation(mouseEvent->modifiers());
        if (dragLength < minimumMarqueePixels) {
            selection_.applyHit(QVector<quint64>{}, operation);
        } else if (const auto scene = sceneSurface_->scene(); scene != nullptr) {
            const auto records =
                selectionRecords(scene->spatialIndex().query(marqueeSceneArea_.normalized()));
            selection_.applyMarquee(records, marqueeSceneArea_, crossing, operation);
        }
        marqueeActive_ = false;
        if (inspectionSelectionHandler_) {
            inspectionSelectionHandler_(selection_.selectedIds(), false);
        }
        interactionSurface_->update();
        return true;
    }
    case QEvent::MouseButtonDblClick:
        if (commandSession_.isActive()) {
            completeCommand();
            return true;
        }
        if (inspectionSelectionHandler_ && !selection_.selectedIds().isEmpty()) {
            inspectionSelectionHandler_(selection_.selectedIds(), true);
            return true;
        }
        return false;
    case QEvent::Wheel: {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        const qreal wheelSteps = wheelEvent->angleDelta().y() != 0
                                     ? static_cast<qreal>(wheelEvent->angleDelta().y()) / 120.0
                                     : static_cast<qreal>(wheelEvent->pixelDelta().y()) / 120.0;
        if (precisionViewport_.zoomAt(wheelEvent->position(), pixelExtent, wheelSteps)) {
            applyViewport();
            updatePointer(wheelEvent->position());
        }
        wheelEvent->accept();
        return true;
    }
    case QEvent::KeyPress: {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Space && !keyEvent->isAutoRepeat()) {
            spacePanEnabled_ = true;
            interactionSurface_->setCursor(Qt::OpenHandCursor);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            commandSession_.cancel();
            marqueeActive_ = false;
            interactionSurface_->update();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            completeCommand();
            return true;
        }
        if (keyEvent->key() == Qt::Key_F7) {
            setGridSnapEnabled(!gridSnapEnabled());
            return true;
        }
        if (keyEvent->key() == Qt::Key_F8) {
            setOrthoEnabled(!orthoEnabled());
            return true;
        }
        if (keyEvent->key() == Qt::Key_F10) {
            setPolarEnabled(!polarEnabled());
            return true;
        }
        return false;
    }
    case QEvent::KeyRelease: {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Space && !keyEvent->isAutoRepeat()) {
            spacePanEnabled_ = false;
            if (!panning_) {
                interactionSurface_->setCursor(Qt::CrossCursor);
            }
            return true;
        }
        return false;
    }
    case QEvent::Enter:
        interactionSurface_->setCursor(Qt::CrossCursor);
        return false;
    case QEvent::Leave:
        pointerAvailable_ = false;
        interactionSurface_->update();
        return false;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

void DrawingWorkspace::updatePointer(const QPointF& pixelPoint) {
    pointerPixel_ = pixelPoint;
    pointerAvailable_ = true;
    const QSizeF pixelExtent = interactionSurface_->size();
    if (!precisionViewport_.isValid(pixelExtent)) {
        return;
    }
    const QPointF scenePoint = precisionViewport_.scenePoint(pixelPoint, pixelExtent);
    const qreal toleranceScene = snapSettings_.tolerancePixels / precisionViewport_.zoom;
    const auto candidates = nearbySnapCandidates(
        sceneSurface_->scene(), scenePoint, toleranceScene, electricalPortSnaps_);
    pointerSnap_ = snapResolver_.resolve(scenePoint,
                                         commandSession_.anchor(),
                                         precisionViewport_,
                                         pixelExtent,
                                         candidates,
                                         snapSettings_);
    commandSession_.updatePointer(pointerSnap_.scenePoint);
}

void DrawingWorkspace::applyViewport() {
    sceneSurface_->setViewport(
        canvas::ViewportState{precisionViewport_.center, precisionViewport_.zoom});
    interactionSurface_->update();
}

void DrawingWorkspace::completeCommand() {
    if (!canonicalEditHandler_) {
        return;
    }
    auto request = commandSession_.complete(selection_.selectedIds());
    if (!request.has_value()) {
        return;
    }
    for (const quint64 itemId : request->selectedItemIds) {
        const QString semanticId = semanticItemIds_.value(itemId);
        if (!semanticId.isEmpty() && !request->semanticIds.contains(semanticId)) {
            request->semanticIds.append(semanticId);
        }
    }
    static_cast<void>(dispatchCanonicalEdit(*request));
    interactionSurface_->update();
}

bool DrawingWorkspace::dispatchCanonicalEdit(const commands::CanonicalEditRequest& request) {
    if (!canonicalEditHandler_ || request.commandId.isEmpty()) {
        return false;
    }
    if (!canonicalEditHandler_(request)) {
        return false;
    }
    ++dispatchedCanonicalEditCount_;
    return true;
}

bool DrawingWorkspace::requestEquipmentPlacement(const QString& catalogId,
                                                 const bool assembly,
                                                 const QPointF& scenePoint) {
    if (!canonicalEditHandler_ || catalogId.trimmed().isEmpty() || !std::isfinite(scenePoint.x()) ||
        !std::isfinite(scenePoint.y()) || commandSession_.isActive() ||
        commandSession_.begin(QStringView{u"equipment.place"}) !=
            commands::CommandStartResult::Started ||
        !commandSession_.acceptPoint(scenePoint, catalogId)) {
        return false;
    }
    auto request = commandSession_.complete({});
    if (!request.has_value()) {
        commandSession_.cancel();
        return false;
    }
    request->attributes.insert(QStringLiteral("catalog_id"), catalogId);
    request->attributes.insert(QStringLiteral("assembly"), assembly);
    return dispatchCanonicalEdit(*request);
}

} // namespace aimora::studio::shell
