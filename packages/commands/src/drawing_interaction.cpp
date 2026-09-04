// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/commands/drawing_interaction.hpp"

#include <QLocale>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace aimora::studio::commands {
namespace {

constexpr qreal comparisonTolerance = 1.0e-9;
constexpr qsizetype maximumCommandPoints = 100'000;

[[nodiscard]] bool finitePoint(const QPointF& point) noexcept {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

[[nodiscard]] bool finiteBounds(const QRectF& bounds) noexcept {
    return std::isfinite(bounds.x()) && std::isfinite(bounds.y())
        && std::isfinite(bounds.width()) && std::isfinite(bounds.height())
        && bounds.width() >= 0.0 && bounds.height() >= 0.0;
}

[[nodiscard]] std::optional<qreal> parseScalar(QStringView input) {
    if (input.isEmpty()) {
        return std::nullopt;
    }
    bool converted = false;
    const qreal value = QLocale::c().toDouble(input.toString(), &converted);
    if (!converted || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<QPointF> parseCartesian(QStringView input) {
    const qsizetype separator = input.indexOf(u',');
    if (separator <= 0 || separator != input.lastIndexOf(u',')) {
        return std::nullopt;
    }
    const auto horizontal = parseScalar(input.first(separator));
    const auto vertical = parseScalar(input.sliced(separator + 1));
    if (!horizontal.has_value() || !vertical.has_value()) {
        return std::nullopt;
    }
    return QPointF{*horizontal, *vertical};
}

[[nodiscard]] int snapPriority(const SnapKind kind) noexcept {
    switch (kind) {
    case SnapKind::ElectricalPort:
        return 0;
    case SnapKind::Endpoint:
        return 1;
    case SnapKind::Intersection:
        return 2;
    case SnapKind::Midpoint:
        return 3;
    case SnapKind::Center:
        return 4;
    case SnapKind::Alignment:
        return 5;
    case SnapKind::Grid:
        return 6;
    case SnapKind::None:
        return 7;
    }
    return 7;
}

[[nodiscard]] QPointF constrainedPoint(const QPointF& raw,
                                       const std::optional<QPointF>& anchor,
                                       const SnapSettings& settings) {
    if (!anchor.has_value()) {
        return raw;
    }
    const QPointF delta = raw - *anchor;
    if (settings.orthoEnabled) {
        if (std::abs(delta.x()) >= std::abs(delta.y())) {
            return *anchor + QPointF{delta.x(), 0.0};
        }
        return *anchor + QPointF{0.0, delta.y()};
    }
    if (settings.polarEnabled) {
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length <= comparisonTolerance) {
            return *anchor;
        }
        const qreal angle = std::atan2(delta.y(), delta.x());
        const qreal increment = settings.polarIncrementDegrees * std::numbers::pi_v<qreal>
            / 180.0;
        const qreal constrainedAngle = std::round(angle / increment) * increment;
        return *anchor
            + QPointF{length * std::cos(constrainedAngle),
                      length * std::sin(constrainedAngle)};
    }
    return raw;
}

struct SnapChoice final {
    QPointF point;
    SnapKind kind{SnapKind::None};
    quint64 itemId{0};
    qreal distancePixels{std::numeric_limits<qreal>::infinity()};
};

[[nodiscard]] bool betterChoice(const SnapChoice& candidate, const SnapChoice& current) {
    if (candidate.distancePixels + comparisonTolerance < current.distancePixels) {
        return true;
    }
    if (std::abs(candidate.distancePixels - current.distancePixels) > comparisonTolerance) {
        return false;
    }
    if (snapPriority(candidate.kind) != snapPriority(current.kind)) {
        return snapPriority(candidate.kind) < snapPriority(current.kind);
    }
    if (candidate.itemId != current.itemId) {
        return candidate.itemId < current.itemId;
    }
    if (candidate.point.x() != current.point.x()) {
        return candidate.point.x() < current.point.x();
    }
    return candidate.point.y() < current.point.y();
}

[[nodiscard]] bool candidateEnabled(const SnapCandidate& candidate,
                                    const SnapSettings& settings) noexcept {
    return candidate.kind == SnapKind::ElectricalPort ? settings.electricalPortEnabled
                                                      : settings.objectEnabled;
}

} // namespace

std::optional<CoordinateInput> CoordinateInterpreter::parse(QStringView input,
                                                            const QPointF& anchor) {
    const QString normalized = input.toString().trimmed();
    if (normalized.isEmpty() || !finitePoint(anchor)) {
        return std::nullopt;
    }
    const QStringView view{normalized};
    if (view.startsWith(u'@')) {
        const QStringView relative = view.sliced(1);
        const qsizetype polarSeparator = relative.indexOf(u'<');
        if (polarSeparator >= 0) {
            if (polarSeparator == 0 || polarSeparator != relative.lastIndexOf(u'<')) {
                return std::nullopt;
            }
            const auto distance = parseScalar(relative.first(polarSeparator));
            const auto degrees = parseScalar(relative.sliced(polarSeparator + 1));
            if (!distance.has_value() || !degrees.has_value() || *distance < 0.0) {
                return std::nullopt;
            }
            const qreal radians = *degrees * std::numbers::pi_v<qreal> / 180.0;
            const QPointF point =
                anchor + QPointF{*distance * std::cos(radians),
                                 *distance * std::sin(radians)};
            return CoordinateInput{point, CoordinateInputKind::Polar};
        }
        const auto delta = parseCartesian(relative);
        if (!delta.has_value()) {
            return std::nullopt;
        }
        return CoordinateInput{anchor + *delta, CoordinateInputKind::Relative};
    }
    const auto point = parseCartesian(view);
    if (!point.has_value()) {
        return std::nullopt;
    }
    return CoordinateInput{*point, CoordinateInputKind::Absolute};
}

bool PrecisionViewport::isValid(const QSizeF& pixelExtent) const noexcept {
    return finitePoint(center) && std::isfinite(zoom) && std::isfinite(minimumZoom)
        && std::isfinite(maximumZoom) && zoom >= minimumZoom && zoom <= maximumZoom
        && minimumZoom > 0.0 && maximumZoom >= minimumZoom && pixelExtent.width() > 0.0
        && pixelExtent.height() > 0.0 && std::isfinite(pixelExtent.width())
        && std::isfinite(pixelExtent.height());
}

QPointF PrecisionViewport::scenePoint(const QPointF& pixel,
                                      const QSizeF& pixelExtent) const noexcept {
    return center
        + QPointF{(pixel.x() - (pixelExtent.width() / 2.0)) / zoom,
                  (pixel.y() - (pixelExtent.height() / 2.0)) / zoom};
}

QPointF PrecisionViewport::pixelPoint(const QPointF& scene,
                                      const QSizeF& pixelExtent) const noexcept {
    return {(scene.x() - center.x()) * zoom + (pixelExtent.width() / 2.0),
            (scene.y() - center.y()) * zoom + (pixelExtent.height() / 2.0)};
}

bool PrecisionViewport::zoomAt(const QPointF& pixel,
                               const QSizeF& pixelExtent,
                               const qreal wheelSteps) noexcept {
    if (!isValid(pixelExtent) || !finitePoint(pixel) || !std::isfinite(wheelSteps)) {
        return false;
    }
    const QPointF fixedScenePoint = scenePoint(pixel, pixelExtent);
    const qreal requestedZoom = zoom * std::pow(1.2, wheelSteps);
    const qreal nextZoom = std::clamp(requestedZoom, minimumZoom, maximumZoom);
    if (!std::isfinite(nextZoom) || std::abs(nextZoom - zoom) <= comparisonTolerance) {
        return false;
    }
    zoom = nextZoom;
    center = fixedScenePoint
        - QPointF{(pixel.x() - (pixelExtent.width() / 2.0)) / zoom,
                  (pixel.y() - (pixelExtent.height() / 2.0)) / zoom};
    return true;
}

void PrecisionViewport::panBy(const QPointF& pixelDelta) noexcept {
    if (!finitePoint(pixelDelta) || !std::isfinite(zoom) || zoom <= 0.0) {
        return;
    }
    center -= pixelDelta / zoom;
}

bool SnapSettings::isValid() const noexcept {
    return std::isfinite(gridSpacing) && gridSpacing > 0.0
        && std::isfinite(tolerancePixels) && tolerancePixels >= 0.0
        && std::isfinite(polarIncrementDegrees) && polarIncrementDegrees > 0.0
        && polarIncrementDegrees <= 180.0 && !(orthoEnabled && polarEnabled);
}

bool SnapResult::snapped() const noexcept {
    return kind != SnapKind::None;
}

SnapResult SnapResolver::resolve(const QPointF& rawScenePoint,
                                 const std::optional<QPointF>& anchor,
                                 const PrecisionViewport& viewport,
                                 const QSizeF& pixelExtent,
                                 const QVector<SnapCandidate>& candidates,
                                 const SnapSettings& settings) const {
    if (!finitePoint(rawScenePoint) || !viewport.isValid(pixelExtent) || !settings.isValid()) {
        return {rawScenePoint, SnapKind::None, 0, {}};
    }
    const QPointF constrained = constrainedPoint(rawScenePoint, anchor, settings);
    const QPointF targetPixel = viewport.pixelPoint(constrained, pixelExtent);
    SnapChoice best;
    auto consider = [&](const QPointF& point, const SnapKind kind, const quint64 itemId) {
        if (!finitePoint(point)) {
            return;
        }
        const qreal distance = QLineF{targetPixel, viewport.pixelPoint(point, pixelExtent)}.length();
        const SnapChoice choice{point, kind, itemId, distance};
        if (distance <= settings.tolerancePixels && betterChoice(choice, best)) {
            best = choice;
        }
    };

    for (const SnapCandidate& candidate : candidates) {
        if (candidateEnabled(candidate, settings)) {
            consider(candidate.point, candidate.kind, candidate.itemId);
        }
    }

    QVector<AlignmentGuide> guides;
    if (settings.alignmentEnabled && !candidates.isEmpty()) {
        std::optional<SnapCandidate> horizontal;
        std::optional<SnapCandidate> vertical;
        qreal horizontalDistance = std::numeric_limits<qreal>::infinity();
        qreal verticalDistance = std::numeric_limits<qreal>::infinity();
        for (const SnapCandidate& candidate : candidates) {
            if (!candidateEnabled(candidate, settings)) {
                continue;
            }
            const QPointF candidatePixel = viewport.pixelPoint(candidate.point, pixelExtent);
            const qreal xDistance = std::abs(candidatePixel.x() - targetPixel.x());
            const qreal yDistance = std::abs(candidatePixel.y() - targetPixel.y());
            if (xDistance <= settings.tolerancePixels && xDistance < verticalDistance) {
                verticalDistance = xDistance;
                vertical = candidate;
            }
            if (yDistance <= settings.tolerancePixels && yDistance < horizontalDistance) {
                horizontalDistance = yDistance;
                horizontal = candidate;
            }
        }
        QPointF aligned = constrained;
        if (vertical.has_value()) {
            aligned.setX(vertical->point.x());
            guides.append({Qt::Vertical, vertical->point.x()});
        }
        if (horizontal.has_value()) {
            aligned.setY(horizontal->point.y());
            guides.append({Qt::Horizontal, horizontal->point.y()});
        }
        if (vertical.has_value() || horizontal.has_value()) {
            consider(aligned, SnapKind::Alignment, 0);
        }
    }

    if (settings.gridEnabled) {
        const QPointF gridPoint{
            std::round(constrained.x() / settings.gridSpacing) * settings.gridSpacing,
            std::round(constrained.y() / settings.gridSpacing) * settings.gridSpacing,
        };
        consider(gridPoint, SnapKind::Grid, 0);
    }

    if (best.kind == SnapKind::None) {
        return {constrained, SnapKind::None, 0, {}};
    }
    return {best.point, best.kind, best.itemId, std::move(guides)};
}

void SelectionModel::clear() noexcept {
    selectedIds_.clear();
}

void SelectionModel::applyHit(const QVector<quint64>& hitIds,
                              const SelectionOperation operation) {
    QVector<quint64> deterministicIds = hitIds;
    std::sort(deterministicIds.begin(), deterministicIds.end());
    deterministicIds.erase(std::unique(deterministicIds.begin(), deterministicIds.end()),
                           deterministicIds.end());
    if (deterministicIds.size() > 1) {
        deterministicIds.resize(1);
    }
    applyIds(deterministicIds, operation);
}

void SelectionModel::applyMarquee(const QVector<SelectionRecord>& records,
                                  const QRectF& area,
                                  const bool crossing,
                                  const SelectionOperation operation) {
    const QRectF normalizedArea = area.normalized();
    QVector<quint64> matches;
    matches.reserve(records.size());
    for (const SelectionRecord& record : records) {
        if (record.itemId == 0 || !finiteBounds(record.bounds)) {
            continue;
        }
        const bool selected = crossing ? normalizedArea.intersects(record.bounds)
                                       : normalizedArea.contains(record.bounds);
        if (selected) {
            matches.append(record.itemId);
        }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    applyIds(matches, operation);
}

bool SelectionModel::contains(const quint64 itemId) const {
    return selectedIds_.contains(itemId);
}

QVector<quint64> SelectionModel::selectedIds() const {
    QVector<quint64> result;
    result.reserve(selectedIds_.size());
    for (const quint64 itemId : selectedIds_) {
        result.append(itemId);
    }
    std::sort(result.begin(), result.end());
    return result;
}

QVector<EditHandle> SelectionModel::handles(const QVector<SelectionRecord>& records) const {
    QVector<EditHandle> result;
    for (const SelectionRecord& record : records) {
        if (!contains(record.itemId) || !finiteBounds(record.bounds)) {
            continue;
        }
        const QRectF bounds = record.bounds.normalized();
        result.append({record.itemId, bounds.topLeft(), EditHandleKind::Grip});
        result.append({record.itemId, bounds.topRight(), EditHandleKind::Grip});
        result.append({record.itemId, bounds.bottomRight(), EditHandleKind::Grip});
        result.append({record.itemId, bounds.bottomLeft(), EditHandleKind::Grip});
        result.append({record.itemId,
                       QPointF{bounds.center().x(), bounds.top()},
                       EditHandleKind::VirtualNode});
        result.append({record.itemId,
                       QPointF{bounds.right(), bounds.center().y()},
                       EditHandleKind::VirtualNode});
        result.append({record.itemId,
                       QPointF{bounds.center().x(), bounds.bottom()},
                       EditHandleKind::VirtualNode});
        result.append({record.itemId,
                       QPointF{bounds.left(), bounds.center().y()},
                       EditHandleKind::VirtualNode});
    }
    return result;
}

qsizetype SelectionModel::size() const noexcept {
    return selectedIds_.size();
}

void SelectionModel::applyIds(const QVector<quint64>& itemIds,
                              const SelectionOperation operation) {
    if (operation == SelectionOperation::Replace) {
        selectedIds_.clear();
    }
    for (const quint64 itemId : itemIds) {
        if (itemId == 0) {
            continue;
        }
        switch (operation) {
        case SelectionOperation::Replace:
        case SelectionOperation::Add:
            selectedIds_.insert(itemId);
            break;
        case SelectionOperation::Toggle:
            if (selectedIds_.contains(itemId)) {
                selectedIds_.remove(itemId);
            } else {
                selectedIds_.insert(itemId);
            }
            break;
        case SelectionOperation::Subtract:
            selectedIds_.remove(itemId);
            break;
        }
    }
}

DrawingCommandSession::DrawingCommandSession()
    : aliases_{{QStringLiteral("l"), QStringLiteral("draw.line")},
               {QStringLiteral("line"), QStringLiteral("draw.line")},
               {QStringLiteral("pl"), QStringLiteral("draw.polyline")},
               {QStringLiteral("polyline"), QStringLiteral("draw.polyline")},
               {QStringLiteral("m"), QStringLiteral("modify.move")},
               {QStringLiteral("move"), QStringLiteral("modify.move")}} {}

CommandStartResult DrawingCommandSession::begin(QStringView commandOrAlias) {
    if (isActive()) {
        return CommandStartResult::AlreadyActive;
    }
    const QString resolved = resolveCommand(commandOrAlias);
    if (resolved.isEmpty()) {
        return CommandStartResult::UnknownCommand;
    }
    activeCommandId_ = resolved;
    points_.clear();
    pointerPoint_.reset();
    return CommandStartResult::Started;
}

bool DrawingCommandSession::acceptPoint(const QPointF& point) {
    if (!isActive() || !finitePoint(point) || points_.size() >= maximumCommandPoints) {
        return false;
    }
    points_.append(point);
    pointerPoint_ = point;
    return true;
}

void DrawingCommandSession::updatePointer(const QPointF& point) {
    if (isActive() && finitePoint(point)) {
        pointerPoint_ = point;
    }
}

std::optional<CanonicalEditRequest>
DrawingCommandSession::complete(const QVector<quint64>& selectedItemIds) {
    if (!isActive()
        || points_.size() < minimumPointCount(QStringView{activeCommandId_})) {
        return std::nullopt;
    }
    CanonicalEditRequest request{
        .serial = nextSerial_,
        .commandId = activeCommandId_,
        .points = points_,
        .selectedItemIds = selectedItemIds,
    };
    ++nextSerial_;
    ++completedEditCount_;
    cancel();
    return request;
}

void DrawingCommandSession::cancel() noexcept {
    activeCommandId_.clear();
    points_.clear();
    pointerPoint_.reset();
}

bool DrawingCommandSession::isActive() const noexcept {
    return !activeCommandId_.isEmpty();
}

QString DrawingCommandSession::activeCommandId() const {
    return activeCommandId_;
}

std::optional<QPointF> DrawingCommandSession::anchor() const {
    if (points_.isEmpty()) {
        return std::nullopt;
    }
    return points_.constLast();
}

CommandPreview DrawingCommandSession::preview() const {
    CommandPreview result;
    result.fixedPoints = points_;
    result.pointerPoint = pointerPoint_;
    if (!isActive() || points_.isEmpty()) {
        return result;
    }
    for (qsizetype index = 1; index < points_.size(); ++index) {
        result.segments.append(QLineF{points_[index - 1], points_[index]});
    }
    if (pointerPoint_.has_value() && *pointerPoint_ != points_.constLast()) {
        result.segments.append(QLineF{points_.constLast(), *pointerPoint_});
    }
    return result;
}

quint64 DrawingCommandSession::completedEditCount() const noexcept {
    return completedEditCount_;
}

qsizetype DrawingCommandSession::minimumPointCount(QStringView commandId) noexcept {
    if (commandId == QStringView{u"draw.line"} || commandId == QStringView{u"draw.polyline"}
        || commandId == QStringView{u"modify.move"}) {
        return 2;
    }
    return std::numeric_limits<qsizetype>::max();
}

QString DrawingCommandSession::resolveCommand(QStringView commandOrAlias) const {
    QString normalized = commandOrAlias.toString().trimmed().toLower();
    if (aliases_.contains(normalized)) {
        return aliases_.value(normalized);
    }
    if (minimumPointCount(QStringView{normalized}) != std::numeric_limits<qsizetype>::max()) {
        return normalized;
    }
    return {};
}

} // namespace aimora::studio::commands
