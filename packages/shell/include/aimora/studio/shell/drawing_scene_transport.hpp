// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/canvas/retained_scene.hpp"
#include <QHash>
#include <QJsonObject>
#include <QStringList>

namespace aimora::studio::shell {

struct DrawingSceneDecodeResult final {
    std::shared_ptr<const canvas::RetainedScene> scene;
    QHash<quint64, QString> ownerIds;
    QStringList unsupportedOwnerIds;
    QString error;
};

[[nodiscard]] DrawingSceneDecodeResult decodeDrawingScene(const QJsonObject& payload,
                                                          const QColor& foreground);

} // namespace aimora::studio::shell
