// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/drawing_scene_transport.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace aimora::studio::shell {

DrawingSceneDecodeResult decodeDrawingScene(const QJsonObject& payload, const QColor& foreground) {
    const auto fail = [](const QString& error) {
        DrawingSceneDecodeResult result;
        result.error = error;
        return result;
    };
    if (!payload.value(QStringLiteral("items")).isArray() ||
        !payload.value(QStringLiteral("unsupported_owner_ids")).isArray()) {
        return fail(QStringLiteral("Invalid drawing display envelope."));
    }
    const QJsonArray items = payload.value(QStringLiteral("items")).toArray();
    const QJsonArray unsupported = payload.value(QStringLiteral("unsupported_owner_ids")).toArray();
    if (items.size() + unsupported.size() > 4096) {
        return fail(QStringLiteral("Drawing display item limit exceeded."));
    }
    DrawingSceneDecodeResult result;
    canvas::SceneDocument document;
    canvas::SceneStyle style;
    style.stroke = foreground;
    document.styles.append(style);
    QSet<QString> owners;
    qsizetype pointCount = 0;
    for (const QJsonValue& value : items) {
        if (!value.isObject()) {
            return fail(QStringLiteral("Invalid drawing display item."));
        }
        const QJsonObject item = value.toObject();
        bool validId = false;
        const quint64 id = item.value(QStringLiteral("item_id")).toString().toULongLong(&validId);
        const QString owner = item.value(QStringLiteral("owner_id")).toString();
        if (!validId || id == 0 || owner.isEmpty() || owner.size() > 1024 ||
            result.ownerIds.contains(id) || owners.contains(owner) ||
            !item.value(QStringLiteral("points")).isArray()) {
            return fail(QStringLiteral("Invalid or duplicate drawing identity."));
        }
        QVector<QPointF> points;
        const QJsonArray coordinates = item.value(QStringLiteral("points")).toArray();
        pointCount += coordinates.size();
        if (pointCount > 100000) {
            return fail(QStringLiteral("Drawing display point limit exceeded."));
        }
        for (const QJsonValue& coordinate : coordinates) {
            const QJsonArray pair = coordinate.toArray();
            if (!coordinate.isArray() || pair.size() != 2 || !pair[0].isDouble() ||
                !pair[1].isDouble() || !std::isfinite(pair[0].toDouble()) ||
                !std::isfinite(pair[1].toDouble())) {
                return fail(QStringLiteral("Invalid drawing coordinate."));
            }
            points.append({pair[0].toDouble(), pair[1].toDouble()});
        }
        const QString kind = item.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("text")) {
            if (points.size() != 1 || !item.value(QStringLiteral("text")).isString()) {
                return fail(QStringLiteral("Invalid drawing text."));
            }
            canvas::SceneTextSource text;
            text.id = id;
            text.text = item.value(QStringLiteral("text")).toString();
            if (text.text.size() > 65536) {
                return fail(QStringLiteral("Drawing text limit exceeded."));
            }
            text.position = points[0];
            QFont font;
            font.setPointSizeF(text.pointSize);
            text.fontFamily = font.family();
            text.bounds = QFontMetricsF{font}.boundingRect(text.text).translated(text.position);
            document.texts.append(text);
        } else if (kind == QStringLiteral("circle")) {
            if (points.size() != 2) {
                return fail(QStringLiteral("A circle requires a center and circumference point."));
            }
            const qreal radius = std::hypot(points[1].x() - points[0].x(),
                                             points[1].y() - points[0].y());
            if (!std::isfinite(radius) || radius <= 0.0) {
                return fail(QStringLiteral("Invalid drawing circle radius."));
            }
            canvas::PrimitiveVector circle;
            circle.id = id;
            circle.kind = canvas::PrimitiveKind::Circle;
            circle.points = {points[0]};
            circle.radii = {radius, radius};
            document.primitives.append(circle);
        } else if (kind == QStringLiteral("line") || kind == QStringLiteral("polyline") ||
                   kind == QStringLiteral("polygon")) {
            canvas::PrimitiveVector primitive;
            primitive.id = id;
            primitive.kind = kind == QStringLiteral("line") ? canvas::PrimitiveKind::Line :
                             kind == QStringLiteral("polygon") ? canvas::PrimitiveKind::Polygon :
                                                                  canvas::PrimitiveKind::Polyline;
            primitive.points = points;
            document.primitives.append(primitive);
        } else {
            return fail(QStringLiteral("Unsupported drawing display primitive."));
        }
        result.ownerIds.insert(id, owner);
        owners.insert(owner);
    }
    for (const QJsonValue& value : unsupported) {
        const QString owner = value.toString();
        if (!value.isString() || owner.isEmpty() || owner.size() > 1024 || owners.contains(owner)) {
            return fail(QStringLiteral("Invalid unsupported drawing identity."));
        }
        result.unsupportedOwnerIds.append(owner);
        owners.insert(owner);
    }
    const auto compiled = canvas::RetainedSceneCompiler{}.compile(document);
    if (!compiled.succeeded()) {
        return fail(QStringLiteral("Drawing scene compilation failed."));
    }
    result.scene = compiled.scene;
    return result;
}

} // namespace aimora::studio::shell
