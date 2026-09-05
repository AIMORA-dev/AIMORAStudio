// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/commands/drawing_interaction.hpp"

#include <QLocale>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace aimora::studio::commands {
namespace {

constexpr qreal comparisonTolerance = 1.0e-9;
constexpr qsizetype maximumCommandPoints = 100'000;

[[nodiscard]] std::optional<QString> canonicalDecimalText(QStringView input) {
    if (input.size() > 4096) {
        return std::nullopt;
    }
    static const QRegularExpression decimal{
        QStringLiteral("\\A([+-]?)(?:([0-9]+)(?:\\.([0-9]*))?|\\.([0-9]+))([eE][+-]?[0-9]+)?\\z")};
    const auto match = decimal.match(input.toString().trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    QString integer = match.captured(2);
    if (integer.isEmpty()) {
        integer = QStringLiteral("0");
    }
    qsizetype firstDigit = 0;
    while (firstDigit + 1 < integer.size() && integer[firstDigit] == QLatin1Char('0')) {
        ++firstDigit;
    }
    integer = integer.sliced(firstDigit);
    const QString fraction = match.captured(2).isEmpty() ? match.captured(4) : match.captured(3);
    QString result = match.captured(1) == QStringLiteral("-") ? QStringLiteral("-") : QString{};
    result += integer;
    if (!fraction.isEmpty()) {
        result += QLatin1Char('.');
        result += fraction;
    }
    result += match.captured(5);
    return result.size() <= 4096 ? std::optional<QString>{result} : std::nullopt;
}

[[nodiscard]] bool finitePoint(const QPointF& point) noexcept {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

[[nodiscard]] bool finiteBounds(const QRectF& bounds) noexcept {
    return std::isfinite(bounds.x()) && std::isfinite(bounds.y()) &&
           std::isfinite(bounds.width()) && std::isfinite(bounds.height()) &&
           bounds.width() >= 0.0 && bounds.height() >= 0.0 &&
           std::isfinite(bounds.right()) && std::isfinite(bounds.bottom());
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
        const qreal increment = settings.polarIncrementDegrees * std::numbers::pi_v<qreal> / 180.0;
        const qreal constrainedAngle = std::round(angle / increment) * increment;
        return *anchor +
               QPointF{length * std::cos(constrainedAngle), length * std::sin(constrainedAngle)};
    }
    return raw;
}

struct SnapChoice final {
    QPointF point;
    SnapKind kind{SnapKind::None};
    quint64 itemId{0};
    qreal distancePixels{std::numeric_limits<qreal>::infinity()};
    QString semanticId;
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

[[nodiscard]] bool alignmentCandidatePrecedes(const SnapCandidate& candidate,
                                              const SnapCandidate& current) {
    if (snapPriority(candidate.kind) != snapPriority(current.kind)) {
        return snapPriority(candidate.kind) < snapPriority(current.kind);
    }
    if (candidate.itemId != current.itemId) {
        return candidate.itemId < current.itemId;
    }
    if (candidate.point.x() != current.point.x()) {
        return candidate.point.x() < current.point.x();
    }
    if (candidate.point.y() != current.point.y()) {
        return candidate.point.y() < current.point.y();
    }
    return candidate.semanticId < current.semanticId;
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
            const qreal radians = std::remainder(*degrees, 360.0) *
                                  (std::numbers::pi_v<qreal> / 180.0);
            const QPointF point =
                anchor + QPointF{*distance * std::cos(radians), *distance * std::sin(radians)};
            if (!finitePoint(point)) {
                return std::nullopt;
            }
            return CoordinateInput{point, CoordinateInputKind::Polar};
        }
        const auto delta = parseCartesian(relative);
        if (!delta.has_value()) {
            return std::nullopt;
        }
        const QPointF point = anchor + *delta;
        if (!finitePoint(point)) {
            return std::nullopt;
        }
        return CoordinateInput{point, CoordinateInputKind::Relative};
    }
    const auto point = parseCartesian(view);
    if (!point.has_value()) {
        return std::nullopt;
    }
    return CoordinateInput{*point, CoordinateInputKind::Absolute};
}

bool PrecisionViewport::isValid(const QSizeF& pixelExtent) const noexcept {
    return finitePoint(center) && std::isfinite(zoom) && std::isfinite(minimumZoom) &&
           std::isfinite(maximumZoom) && zoom >= minimumZoom && zoom <= maximumZoom &&
           minimumZoom > 0.0 && maximumZoom >= minimumZoom && pixelExtent.width() > 0.0 &&
           pixelExtent.height() > 0.0 && std::isfinite(pixelExtent.width()) &&
           std::isfinite(pixelExtent.height());
}

QPointF PrecisionViewport::scenePoint(const QPointF& pixel,
                                      const QSizeF& pixelExtent) const noexcept {
    return center + QPointF{(pixel.x() - (pixelExtent.width() / 2.0)) / zoom,
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
    const QPointF nextCenter = fixedScenePoint -
        QPointF{(pixel.x() - (pixelExtent.width() / 2.0)) / nextZoom,
                (pixel.y() - (pixelExtent.height() / 2.0)) / nextZoom};
    if (!finitePoint(fixedScenePoint) || !finitePoint(nextCenter)) {
        return false;
    }
    zoom = nextZoom;
    center = nextCenter;
    return true;
}

void PrecisionViewport::panBy(const QPointF& pixelDelta) noexcept {
    if (!finitePoint(pixelDelta) || !std::isfinite(zoom) || zoom <= 0.0) {
        return;
    }
    const QPointF nextCenter = center - pixelDelta / zoom;
    if (finitePoint(nextCenter)) {
        center = nextCenter;
    }
}

bool PrecisionViewport::fitBounds(const QRectF& bounds, const QSizeF& pixelExtent,
                                  const qreal paddingPixels) noexcept {
    const QRectF normalized = bounds.normalized();
    if (!isValid(pixelExtent) || !finiteBounds(normalized) ||
        !std::isfinite(paddingPixels) || paddingPixels < 0.0) {
        return false;
    }
    const qreal availableWidth = pixelExtent.width() - 2.0 * paddingPixels;
    const qreal availableHeight = pixelExtent.height() - 2.0 * paddingPixels;
    if (availableWidth <= 0.0 || availableHeight <= 0.0) {
        return false;
    }
    qreal fittedZoom = maximumZoom;
    if (normalized.width() > 0.0) {
        fittedZoom = std::min(fittedZoom, availableWidth / normalized.width());
    }
    if (normalized.height() > 0.0) {
        fittedZoom = std::min(fittedZoom, availableHeight / normalized.height());
    }
    const QPointF fittedCenter{normalized.left() / 2.0 + normalized.right() / 2.0,
                               normalized.top() / 2.0 + normalized.bottom() / 2.0};
    if (!std::isfinite(fittedZoom) || fittedZoom < minimumZoom || !finitePoint(fittedCenter)) {
        return false;
    }
    center = fittedCenter;
    zoom = fittedZoom;
    return true;
}

bool SnapSettings::isValid() const noexcept {
    return std::isfinite(gridSpacing) && gridSpacing > 0.0 && std::isfinite(tolerancePixels) &&
           tolerancePixels >= 0.0 && std::isfinite(polarIncrementDegrees) &&
           polarIncrementDegrees > 0.0 && polarIncrementDegrees <= 180.0 &&
           !(orthoEnabled && polarEnabled);
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
        return {rawScenePoint, SnapKind::None, 0, {}, {}};
    }
    const QPointF constrained = constrainedPoint(rawScenePoint, anchor, settings);
    if (!finitePoint(constrained)) {
        return {rawScenePoint, SnapKind::None, 0, {}, {}};
    }
    const QPointF targetPixel = viewport.pixelPoint(constrained, pixelExtent);
    SnapChoice best;
    auto consider = [&](const QPointF& point,
                        const SnapKind kind,
                        const quint64 itemId,
                        const QString& semanticId = QString{}) {
        if (!finitePoint(point)) {
            return;
        }
        const qreal distance =
            QLineF{targetPixel, viewport.pixelPoint(point, pixelExtent)}.length();
        const SnapChoice choice{point, kind, itemId, distance, semanticId};
        if (distance <= settings.tolerancePixels && betterChoice(choice, best)) {
            best = choice;
        }
    };

    for (const SnapCandidate& candidate : candidates) {
        if (candidateEnabled(candidate, settings)) {
            consider(candidate.point, candidate.kind, candidate.itemId, candidate.semanticId);
        }
    }

    QVector<AlignmentGuide> guides;
    if (settings.alignmentEnabled && !candidates.isEmpty()) {
        std::optional<SnapCandidate> horizontal;
        std::optional<SnapCandidate> vertical;
        qreal horizontalDistance = std::numeric_limits<qreal>::infinity();
        qreal verticalDistance = std::numeric_limits<qreal>::infinity();
        for (const SnapCandidate& candidate : candidates) {
            if (!candidateEnabled(candidate, settings) || !finitePoint(candidate.point)) {
                continue;
            }
            const QPointF candidatePixel = viewport.pixelPoint(candidate.point, pixelExtent);
            const qreal xDistance = std::abs(candidatePixel.x() - targetPixel.x());
            const qreal yDistance = std::abs(candidatePixel.y() - targetPixel.y());
            if (xDistance <= settings.tolerancePixels &&
                (xDistance < verticalDistance ||
                 (xDistance == verticalDistance && vertical.has_value() &&
                  alignmentCandidatePrecedes(candidate, *vertical)))) {
                verticalDistance = xDistance;
                vertical = candidate;
            }
            if (yDistance <= settings.tolerancePixels &&
                (yDistance < horizontalDistance ||
                 (yDistance == horizontalDistance && horizontal.has_value() &&
                  alignmentCandidatePrecedes(candidate, *horizontal)))) {
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
        return {constrained, SnapKind::None, 0, {}, {}};
    }
    guides.erase(std::remove_if(guides.begin(), guides.end(), [&](const AlignmentGuide& guide) {
        const qreal coordinate = guide.orientation == Qt::Vertical ? best.point.x() : best.point.y();
        return coordinate != guide.coordinate;
    }), guides.end());
    return {best.point, best.kind, best.itemId, std::move(guides), best.semanticId};
}

void SelectionModel::clear() noexcept {
    selectedIds_.clear();
}

void SelectionModel::replaceSelection(const QVector<quint64>& itemIds) {
    applyIds(itemIds, SelectionOperation::Replace);
}

void SelectionModel::applyHit(const QVector<quint64>& hitIds, const SelectionOperation operation) {
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
    if (!finiteBounds(normalizedArea)) {
        return;
    }
    QVector<quint64> matches;
    matches.reserve(records.size());
    for (const SelectionRecord& record : records) {
        if (record.itemId == 0 || !finiteBounds(record.bounds)) {
            continue;
        }
        // Closed interval comparisons include line and point bounds, which
        // QRectF::intersects treats as empty rectangles.
        const QRectF& bounds = record.bounds;
        const bool selected = crossing
                                  ? normalizedArea.left() <= bounds.right() &&
                                        bounds.left() <= normalizedArea.right() &&
                                        normalizedArea.top() <= bounds.bottom() &&
                                        bounds.top() <= normalizedArea.bottom()
                                  : normalizedArea.left() <= bounds.left() &&
                                        bounds.right() <= normalizedArea.right() &&
                                        normalizedArea.top() <= bounds.top() &&
                                        bounds.bottom() <= normalizedArea.bottom();
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

void SelectionModel::applyIds(const QVector<quint64>& itemIds, const SelectionOperation operation) {
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
               {QStringLiteral("rec"), QStringLiteral("draw.rectangle")},
               {QStringLiteral("rectangle"), QStringLiteral("draw.rectangle")},
               {QStringLiteral("t"), QStringLiteral("draw.text")},
               {QStringLiteral("text"), QStringLiteral("draw.text")},
               {QStringLiteral("c"), QStringLiteral("draw.circle")},
               {QStringLiteral("circle"), QStringLiteral("draw.circle")},
               {QStringLiteral("a"), QStringLiteral("draw.arc")},
               {QStringLiteral("arc"), QStringLiteral("draw.arc")},
               {QStringLiteral("el"), QStringLiteral("draw.ellipse")},
               {QStringLiteral("ellipse"), QStringLiteral("draw.ellipse")},
               {QStringLiteral("mih"), QStringLiteral("modify.mirror_horizontal")},
               {QStringLiteral("miv"), QStringLiteral("modify.mirror_vertical")},
               {QStringLiteral("r90"), QStringLiteral("modify.rotate_quarter")},
               {QStringLiteral("ax"), QStringLiteral("modify.align_anchor_x")},
               {QStringLiteral("ed"), QStringLiteral("modify.text")},
               {QStringLiteral("ddedit"), QStringLiteral("modify.text")},
               {QStringLiteral("x"), QStringLiteral("modify.explode_paths")},
               {QStringLiteral("explode"), QStringLiteral("modify.explode_paths")},
               {QStringLiteral("j"), QStringLiteral("modify.join_lines")},
               {QStringLiteral("join"), QStringLiteral("modify.join_lines")},
               {QStringLiteral("ay"), QStringLiteral("modify.align_anchor_y")},
               {QStringLiteral("dx"), QStringLiteral("modify.distribute_anchor_x")},
               {QStringLiteral("dy"), QStringLiteral("modify.distribute_anchor_y")},
               {QStringLiteral("undo"), QStringLiteral("edit.undo")},
               {QStringLiteral("redo"), QStringLiteral("edit.redo")},
               {QStringLiteral("m"), QStringLiteral("modify.move")},
               {QStringLiteral("move"), QStringLiteral("modify.move")},
               {QStringLiteral("co"), QStringLiteral("modify.copy")},
               {QStringLiteral("cp"), QStringLiteral("modify.copy")},
               {QStringLiteral("copy"), QStringLiteral("modify.copy")},
               {QStringLiteral("sc"), QStringLiteral("modify.scale")},
               {QStringLiteral("scale"), QStringLiteral("modify.scale")},
               {QStringLiteral("erase"), QStringLiteral("modify.erase")},
               {QStringLiteral("e"), QStringLiteral("modify.erase")},
               {QStringLiteral("wire"), QStringLiteral("electrical.connect")},
               {QStringLiteral("connect"), QStringLiteral("electrical.connect")},
               {QStringLiteral("place"), QStringLiteral("equipment.place")},
               {QStringLiteral("junction"), QStringLiteral("junction.update")},
               {QStringLiteral("removeprojection"), QStringLiteral("projection.remove")},
               {QStringLiteral("deleteasset"), QStringLiteral("asset.delete")},
               {QStringLiteral("layout"), QStringLiteral("layout.full")},
               {QStringLiteral("layoutinitial"), QStringLiteral("layout.initial")},
               {QStringLiteral("layoutfull"), QStringLiteral("layout.full")},
               {QStringLiteral("layoutlocal"), QStringLiteral("layout.local")},
               {QStringLiteral("layoutincremental"), QStringLiteral("layout.incremental")}} {}

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
    pointSemanticIds_.clear();
    pointerPoint_.reset();
    return CommandStartResult::Started;
}

bool DrawingCommandSession::setCustomAliases(const QHash<QString, QString>& aliases) {
    if (isActive() || aliases.size() > 64) {
        return false;
    }
    const QSet<QString> reserved{QStringLiteral("cancel"), QStringLiteral("grid"),
        QStringLiteral("ortho"), QStringLiteral("polar"), QStringLiteral("snap"),
        QStringLiteral("select"), QStringLiteral("pan"), QStringLiteral("zoom"),
        QStringLiteral("gridview"), QStringLiteral("z"), QStringLiteral("ze"),
        QStringLiteral("zoomextents"), QStringLiteral("selectall")};
    QHash<QString, QString> normalized;
    for (auto iterator = aliases.cbegin(); iterator != aliases.cend(); ++iterator) {
        const QString alias = iterator.key().trimmed().toLower();
        const QString command = iterator.value().trimmed().toLower();
        const bool portable = !alias.isEmpty() && alias.size() <= 16 &&
            std::all_of(alias.cbegin(), alias.cend(), [](QChar character) {
                return (character >= QLatin1Char('a') && character <= QLatin1Char('z')) ||
                       (character >= QLatin1Char('0') && character <= QLatin1Char('9'));
            }) && alias.front() >= QLatin1Char('a') && alias.front() <= QLatin1Char('z');
        if (!portable || reserved.contains(alias) || normalized.contains(alias) ||
            minimumPointCount(QStringView{command}) == std::numeric_limits<qsizetype>::max()) {
            return false;
        }
        normalized.insert(alias, command);
    }
    customAliases_ = std::move(normalized);
    return true;
}

QHash<QString, QString> DrawingCommandSession::commandAliases() const {
    auto result = aliases_;
    for (auto iterator = customAliases_.cbegin(); iterator != customAliases_.cend(); ++iterator) {
        result.insert(iterator.key(), iterator.value());
    }
    return result;
}

bool DrawingCommandSession::acceptPoint(const QPointF& point, QString semanticId) {
    if (activeCommandId_ == QStringLiteral("modify.join_lines") ||
        activeCommandId_ == QStringLiteral("modify.explode_paths") ||
        activeCommandId_ == QStringLiteral("modify.text") ||
        activeCommandId_.startsWith(QStringLiteral("modify.distribute_anchor_")) ||
        (activeCommandId_.startsWith(QStringLiteral("modify.align_anchor_")) && !points_.isEmpty())) {
        return false;
    }
    if (!isActive() || !finitePoint(point) || points_.size() >= maximumCommandPoints) {
        return false;
    }
    if (activeCommandId_ == QStringLiteral("draw.arc") && points_.size() >= 3) {
        return false;
    }
    if (activeCommandId_ == QStringLiteral("draw.ellipse") && points_.size() >= 2) {
        return false;
    }
    if ((activeCommandId_ == QStringLiteral("modify.mirror_horizontal") ||
         activeCommandId_ == QStringLiteral("modify.rotate_quarter") ||
         activeCommandId_ == QStringLiteral("modify.mirror_vertical")) && !points_.isEmpty()) {
        return false;
    }
    if ((activeCommandId_ == QStringLiteral("draw.rectangle") ||
         activeCommandId_ == QStringLiteral("draw.circle")) && points_.size() >= 2) {
        return false;
    }
    if ((activeCommandId_ == QStringLiteral("draw.text") ||
         activeCommandId_ == QStringLiteral("modify.scale")) && !points_.isEmpty()) {
        return false;
    }
    attributes_.remove(QStringLiteral("close_path"));
    points_.append(point);
    pointSemanticIds_.append(std::move(semanticId));
    coordinateInputs_.append(QJsonValue{QJsonValue::Null});
    pointerPoint_ = point;
    return true;
}

bool DrawingCommandSession::acceptCoordinateInput(QStringView input, const QPointF& anchor) {
    const auto coordinate = CoordinateInterpreter::parse(input, anchor);
    if (!coordinate.has_value()) {
        return false;
    }
    QString canonicalInput;
    if (coordinate->kind != CoordinateInputKind::Polar) {
        QString text = input.toString().trimmed();
        const bool relative = text.startsWith(QLatin1Char('@'));
        if (relative) {
            text.remove(0, 1);
        }
        const qsizetype separator = text.indexOf(QLatin1Char(','));
        if (separator < 0) {
            return false;
        }
        const auto horizontal = canonicalDecimalText(QStringView{text}.first(separator));
        const auto vertical = canonicalDecimalText(QStringView{text}.sliced(separator + 1));
        if (!horizontal.has_value() || !vertical.has_value()) {
            return false;
        }
        canonicalInput = (relative ? QStringLiteral("@") : QString{}) + *horizontal +
                         QLatin1Char(',') + *vertical;
    }
    if (!acceptPoint(coordinate->point)) {
        return false;
    }
    if (coordinate->kind != CoordinateInputKind::Polar) {
        coordinateInputs_.replace(coordinateInputs_.size() - 1, QJsonObject{
            {QStringLiteral("text"), canonicalInput},
            {QStringLiteral("anchor"), QJsonArray{QString::number(anchor.x(), 'g', 17),
                                                   QString::number(anchor.y(), 'g', 17)}},
        });
    }
    return true;
}

bool DrawingCommandSession::undoPolylineVertex() {
    return activeCommandId_ == QStringLiteral("draw.polyline") && undoPathVertex();
}

bool DrawingCommandSession::undoPathVertex() {
    if ((activeCommandId_ != QStringLiteral("draw.polyline") &&
         activeCommandId_ != QStringLiteral("draw.line") &&
         activeCommandId_ != QStringLiteral("draw.arc")) || points_.isEmpty()) {
        return false;
    }
    attributes_.remove(QStringLiteral("close_path"));
    points_.removeLast();
    pointSemanticIds_.removeLast();
    coordinateInputs_.removeLast();
    pointerPoint_ = anchor();
    return true;
}

bool DrawingCommandSession::closePolyline() {
    return activeCommandId_ == QStringLiteral("draw.polyline") && closePath();
}

bool DrawingCommandSession::closePath() {
    if ((activeCommandId_ != QStringLiteral("draw.polyline") &&
         activeCommandId_ != QStringLiteral("draw.line")) || points_.size() < 3) {
        return false;
    }
    if (points_.first() == points_.last()) {
        attributes_.insert(QStringLiteral("close_path"), true);
        return true;
    }
    const QPointF first = points_.first();
    if (!acceptPoint(first)) {
        return false;
    }
    coordinateInputs_.replace(coordinateInputs_.size() - 1,
                              QJsonObject{{QStringLiteral("reference"), 0}});
    attributes_.insert(QStringLiteral("close_path"), true);
    return true;
}

bool DrawingCommandSession::acceptCircleRadius(QStringView input) {
    if (activeCommandId_ != QStringLiteral("draw.circle") || points_.size() != 1) {
        return false;
    }
    const auto canonicalRadius = canonicalDecimalText(input);
    if (!canonicalRadius.has_value()) {
        return false;
    }
    const QString& radiusText = *canonicalRadius;
    const auto radius = parseScalar(QStringView{radiusText});
    if (!radius.has_value() || *radius <= 0.0) {
        return false;
    }
    const QPointF center = points_.first();
    const QPointF edge{center.x() + *radius, center.y()};
    if (!finitePoint(edge) || edge.x() == center.x()) {
        return false;
    }
    if (!acceptPoint(edge)) {
        return false;
    }
    coordinateInputs_.replace(coordinateInputs_.size() - 1,
        QJsonObject{{QStringLiteral("text"), QStringLiteral("@%1,0").arg(radiusText)}});
    return true;
}

bool DrawingCommandSession::acceptScaleFactor(QStringView input) {
    if (activeCommandId_ != QStringLiteral("modify.scale") || points_.size() != 1) {
        return false;
    }
    const auto canonicalFactor = canonicalDecimalText(input);
    if (!canonicalFactor.has_value() || canonicalFactor->startsWith(QLatin1Char('-'))) {
        return false;
    }
    const QString& factor = *canonicalFactor;
    const qsizetype exponent = factor.indexOf(QLatin1Char('e'), 0, Qt::CaseInsensitive);
    const QStringView mantissa = exponent < 0 ? QStringView{factor} : QStringView{factor}.first(exponent);
    const bool nonzero = std::any_of(mantissa.begin(), mantissa.end(), [](QChar digit) {
        return digit >= QLatin1Char('1') && digit <= QLatin1Char('9');
    });
    if (!nonzero) {
        return false;
    }
    attributes_.insert(QStringLiteral("factor"), factor);
    return true;
}

void DrawingCommandSession::updatePointer(const QPointF& point) {
    if (isActive() && finitePoint(point)) {
        pointerPoint_ = point;
    }
}

std::optional<CanonicalEditRequest>
DrawingCommandSession::complete(const QVector<quint64>& selectedItemIds) {
    if (!isActive() || points_.size() < minimumPointCount(QStringView{activeCommandId_})) {
        return std::nullopt;
    }
    if (activeCommandId_ == QStringLiteral("electrical.connect") &&
        (pointSemanticIds_.size() != points_.size() || pointSemanticIds_.front().isEmpty() ||
         pointSemanticIds_.back().isEmpty() ||
         pointSemanticIds_.front() == pointSemanticIds_.back())) {
        return std::nullopt;
    }
    QStringList semanticIds;
    const bool draftingCommand = activeCommandId_.startsWith(QStringLiteral("draw.")) ||
                                 activeCommandId_.startsWith(QStringLiteral("modify."));
    for (const QString& semanticId : pointSemanticIds_) {
        if (!draftingCommand && !semanticId.isEmpty() && !semanticIds.contains(semanticId)) {
            semanticIds.append(semanticId);
        }
    }
    QJsonObject attributes = attributes_;
    if (draftingCommand && !points_.isEmpty()) {
        attributes.insert(QStringLiteral("coordinate_inputs"), coordinateInputs_);
    }
    CanonicalEditRequest request{
        .serial = nextSerial_,
        .commandId = activeCommandId_,
        .points = points_,
        .selectedItemIds = selectedItemIds,
        .semanticIds = semanticIds,
        .attributes = std::move(attributes),
    };
    ++nextSerial_;
    ++completedEditCount_;
    cancel();
    return request;
}

void DrawingCommandSession::cancel() noexcept {
    activeCommandId_.clear();
    points_.clear();
    pointSemanticIds_.clear();
    attributes_ = {};
    coordinateInputs_ = {};
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
    if (activeCommandId_.startsWith(QStringLiteral("modify.align_anchor_")) ||
        activeCommandId_ == QStringLiteral("draw.text") ||
        activeCommandId_ == QStringLiteral("modify.rotate_quarter") ||
        activeCommandId_ == QStringLiteral("modify.mirror_horizontal") ||
        activeCommandId_ == QStringLiteral("modify.mirror_vertical") ||
        activeCommandId_ == QStringLiteral("modify.scale")) {
        return result;
    }
    if (activeCommandId_ == QStringLiteral("draw.ellipse")) {
        const QPointF first = points_.first();
        const QPointF opposite = points_.size() == 2 ? points_.last() : pointerPoint_.value_or(first);
        const QPointF center = first / 2 + opposite / 2;
        const qreal radiusX = std::abs(opposite.x() / 2 - first.x() / 2);
        const qreal radiusY = std::abs(opposite.y() / 2 - first.y() / 2);
        if (!finitePoint(center) || !std::isfinite(radiusX) || !std::isfinite(radiusY) ||
            radiusX <= 0 || radiusY <= 0) {
            return result;
        }
        constexpr int ellipsePreviewSegments = 64;
        const QPointF initial{center.x() + radiusX, center.y()};
        QPointF previous = initial;
        for (int index = 1; index <= ellipsePreviewSegments; ++index) {
            const qreal angle = 2 * std::numbers::pi_v<qreal> * index / ellipsePreviewSegments;
            const QPointF next = index == ellipsePreviewSegments ? initial :
                center + QPointF{radiusX * std::cos(angle), radiusY * std::sin(angle)};
            if (!finitePoint(previous) || !finitePoint(next)) {
                result.segments.clear();
                return result;
            }
            result.segments.append(QLineF{previous, next});
            previous = next;
        }
        return result;
    }
    if (activeCommandId_ == QStringLiteral("draw.arc") && points_.size() >= 2) {
        const QPointF start = points_[0];
        const QPointF through = points_[1];
        const QPointF finish = points_.size() == 3 ? points_[2] : pointerPoint_.value_or(through);
        const QPointF firstDelta = through - start;
        const QPointF lastDelta = finish - start;
        const qreal scale = std::max({std::abs(firstDelta.x()), std::abs(firstDelta.y()),
                                     std::abs(lastDelta.x()), std::abs(lastDelta.y())});
        if (!std::isfinite(scale) || scale <= 0) {
            return result;
        }
        const QPointF first = firstDelta / scale;
        const QPointF last = lastDelta / scale;
        const qreal cross = first.x() * last.y() - first.y() * last.x();
        if (cross == 0) {
            return result;
        }
        const qreal firstSquared = first.x() * first.x() + first.y() * first.y();
        const qreal lastSquared = last.x() * last.x() + last.y() * last.y();
        const QPointF offset{
            scale * (firstSquared * last.y() - lastSquared * first.y()) / (2 * cross),
            scale * (first.x() * lastSquared - last.x() * firstSquared) / (2 * cross)};
        const QPointF center = start + offset;
        const qreal radius = std::hypot(offset.x(), offset.y());
        if (!finitePoint(center) || !std::isfinite(radius) || radius <= 0) {
            return result;
        }
        const qreal startAngle = std::atan2(-offset.y(), -offset.x());
        const qreal throughAngle = std::atan2(firstDelta.y() - offset.y(), firstDelta.x() - offset.x());
        const qreal finishAngle = std::atan2(lastDelta.y() - offset.y(), lastDelta.x() - offset.x());
        QPointF previous = start;
        const auto appendLeg = [&](qreal angle, qreal endAngle, const QPointF& endpoint) {
            constexpr qreal fullCircle = 2 * std::numbers::pi_v<qreal>;
            qreal sweep = std::fmod(endAngle - angle, fullCircle);
            if (sweep < 0) {
                sweep += fullCircle;
            }
            if (sweep == 0) {
                return false;
            }
            if (cross < 0) {
                sweep -= fullCircle;
            }
            const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) * 64 / fullCircle)));
            for (int index = 1; index <= segments; ++index) {
                const qreal theta = angle + sweep * index / segments;
                const QPointF next = index == segments ? endpoint :
                    center + QPointF{radius * std::cos(theta), radius * std::sin(theta)};
                if (!finitePoint(next)) {
                    return false;
                }
                result.segments.append(QLineF{previous, next});
                previous = next;
            }
            return true;
        };
        if (!appendLeg(startAngle, throughAngle, through) || !appendLeg(throughAngle, finishAngle, finish)) {
            result.segments.clear();
        }
        return result;
    }
    if (activeCommandId_ == QStringLiteral("draw.circle")) {
        const QPointF center = points_.first();
        const QPointF edge = points_.size() == 2 ? points_.last() : pointerPoint_.value_or(center);
        const qreal radius = std::hypot(edge.x() - center.x(), edge.y() - center.y());
        if (!std::isfinite(radius) || radius <= 0.0) {
            return result;
        }
        constexpr int circlePreviewSegments = 64;
        const QPointF first{center.x() + radius, center.y()};
        if (!finitePoint(first) || !finitePoint({center.x() - radius, center.y() - radius}) ||
            !finitePoint({center.x() + radius, center.y() + radius})) {
            return result;
        }
        QPointF previous = first;
        for (int index = 1; index <= circlePreviewSegments; ++index) {
            const qreal angle = 2.0 * std::numbers::pi_v<qreal> * index / circlePreviewSegments;
            const QPointF next = index == circlePreviewSegments ? first :
                center + QPointF{radius * std::cos(angle), radius * std::sin(angle)};
            result.segments.append(QLineF{previous, next});
            previous = next;
        }
        return result;
    }
    if (activeCommandId_ == QStringLiteral("draw.rectangle")) {
        const QPointF opposite = points_.size() >= 2 ? points_[1] :
                                 pointerPoint_.value_or(points_.first());
        const QRectF rectangle = QRectF{points_.first(), opposite}.normalized();
        result.segments = {QLineF{rectangle.topLeft(), rectangle.topRight()},
                           QLineF{rectangle.topRight(), rectangle.bottomRight()},
                           QLineF{rectangle.bottomRight(), rectangle.bottomLeft()},
                           QLineF{rectangle.bottomLeft(), rectangle.topLeft()}};
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
    if (commandId == QStringView{u"draw.arc"}) {
        return 3;
    }
    if (commandId == QStringView{u"draw.line"} || commandId == QStringView{u"draw.polyline"} ||
        commandId == QStringView{u"draw.rectangle"} ||
        commandId == QStringView{u"draw.circle"} ||
        commandId == QStringView{u"draw.ellipse"} ||
        commandId == QStringView{u"modify.move"} ||
        commandId == QStringView{u"modify.copy"} ||
        commandId == QStringView{u"electrical.connect"} ||
        commandId == QStringView{u"route.edit"} || commandId == QStringView{u"projection.edit"}) {
        return 2;
    }
    if (commandId == QStringView{u"modify.align_anchor_x"} ||
        commandId == QStringView{u"modify.align_anchor_y"} ||
        commandId == QStringView{u"equipment.place"} ||
        commandId == QStringView{u"modify.rotate_quarter"} ||
        commandId == QStringView{u"modify.mirror_horizontal"} ||
        commandId == QStringView{u"modify.mirror_vertical"} ||
        commandId == QStringView{u"modify.scale"} ||
        commandId == QStringView{u"draw.text"} ||
        commandId == QStringView{u"junction.update"} ||
        commandId == QStringView{u"cross_reference.update"}) {
        return 1;
    }
    if (commandId == QStringView{u"modify.join_lines"} ||
        commandId == QStringView{u"modify.explode_paths"} ||
        commandId == QStringView{u"modify.text"} ||
        commandId == QStringView{u"modify.distribute_anchor_x"} ||
        commandId == QStringView{u"modify.distribute_anchor_y"} ||
        commandId == QStringView{u"modify.erase"} ||
        commandId == QStringView{u"edit.undo"} || commandId == QStringView{u"edit.redo"} ||
        commandId == QStringView{u"projection.remove"} ||
        commandId == QStringView{u"asset.delete"}) {
        return 0;
    }
    if (commandId == QStringView{u"layout.initial"} || commandId == QStringView{u"layout.full"} ||
        commandId == QStringView{u"layout.local"} ||
        commandId == QStringView{u"layout.incremental"}) {
        return 0;
    }
    return std::numeric_limits<qsizetype>::max();
}

QString DrawingCommandSession::resolveCommand(QStringView commandOrAlias) const {
    QString normalized = commandOrAlias.toString().trimmed().toLower();
    if (customAliases_.contains(normalized)) {
        return customAliases_.value(normalized);
    }
    if (aliases_.contains(normalized)) {
        return aliases_.value(normalized);
    }
    if (minimumPointCount(QStringView{normalized}) != std::numeric_limits<qsizetype>::max()) {
        return normalized;
    }
    return {};
}

} // namespace aimora::studio::commands
