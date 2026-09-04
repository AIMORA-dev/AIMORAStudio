// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QString>
#include <QStringView>
#include <cstdint>
#include <optional>

namespace aimora::studio::themes {

enum class ThemeMode : std::uint8_t {
    System,
    Light,
    Dark,
};

[[nodiscard]] QString toString(ThemeMode mode);
[[nodiscard]] std::optional<ThemeMode> parseThemeMode(QStringView value);

} // namespace aimora::studio::themes
