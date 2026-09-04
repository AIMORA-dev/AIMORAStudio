// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/canvas/retained_scene.hpp"

#include <QString>
#include <QVector>
#include <memory>

namespace aimora::studio::canvas {

struct SceneCacheLimits final {
    qsizetype maximumEntries{8};
    qsizetype maximumBytes{768 * 1024 * 1024};

    [[nodiscard]] bool isValid() const noexcept;
};

class RetainedSceneCache final {
  public:
    explicit RetainedSceneCache(SceneCacheLimits limits = {});

    [[nodiscard]] std::shared_ptr<const RetainedScene> find(const QString& key);
    [[nodiscard]] bool insert(QString key, std::shared_ptr<const RetainedScene> scene);
    void clear() noexcept;

    [[nodiscard]] qsizetype entryCount() const noexcept;
    [[nodiscard]] qsizetype estimatedBytes() const noexcept;
    [[nodiscard]] const SceneCacheLimits& limits() const noexcept;

  private:
    struct Entry final {
        QString key;
        std::shared_ptr<const RetainedScene> scene;
        quint64 access{0};
    };

    void evictToLimits();

    SceneCacheLimits limits_;
    QVector<Entry> entries_;
    qsizetype estimatedBytes_{0};
    quint64 accessCounter_{0};
};

} // namespace aimora::studio::canvas
