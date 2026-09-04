// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/scene_cache.hpp"

#include <algorithm>
#include <utility>

namespace aimora::studio::canvas {

bool SceneCacheLimits::isValid() const noexcept {
    return maximumEntries > 0 && maximumBytes > 0;
}

RetainedSceneCache::RetainedSceneCache(SceneCacheLimits limits)
    : limits_{limits.isValid() ? limits : SceneCacheLimits{}} {}

std::shared_ptr<const RetainedScene> RetainedSceneCache::find(const QString& key) {
    const auto found = std::find_if(
        entries_.begin(), entries_.end(), [&key](const Entry& entry) { return entry.key == key; });
    if (found == entries_.end()) {
        return {};
    }
    found->access = ++accessCounter_;
    return found->scene;
}

bool RetainedSceneCache::insert(QString key, std::shared_ptr<const RetainedScene> scene) {
    if (key.isEmpty() || scene == nullptr || !scene->isValid()) {
        return false;
    }
    const qsizetype bytes = scene->statistics().estimatedBytes;
    if (bytes > limits_.maximumBytes) {
        return false;
    }
    const auto existing = std::find_if(
        entries_.begin(), entries_.end(), [&key](const Entry& entry) { return entry.key == key; });
    if (existing != entries_.end()) {
        estimatedBytes_ -= existing->scene->statistics().estimatedBytes;
        entries_.erase(existing);
    }
    entries_.push_back({std::move(key), std::move(scene), ++accessCounter_});
    estimatedBytes_ += bytes;
    evictToLimits();
    return true;
}

void RetainedSceneCache::evictToLimits() {
    while (entries_.size() > limits_.maximumEntries || estimatedBytes_ > limits_.maximumBytes) {
        const auto victim = std::min_element(
            entries_.begin(), entries_.end(), [](const Entry& left, const Entry& right) {
                if (left.access != right.access) {
                    return left.access < right.access;
                }
                return left.key < right.key;
            });
        estimatedBytes_ -= victim->scene->statistics().estimatedBytes;
        entries_.erase(victim);
    }
}

void RetainedSceneCache::clear() noexcept {
    entries_.clear();
    estimatedBytes_ = 0;
}

qsizetype RetainedSceneCache::entryCount() const noexcept {
    return entries_.size();
}
qsizetype RetainedSceneCache::estimatedBytes() const noexcept {
    return estimatedBytes_;
}
const SceneCacheLimits& RetainedSceneCache::limits() const noexcept {
    return limits_;
}

} // namespace aimora::studio::canvas
