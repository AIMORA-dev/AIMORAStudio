// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/themes/theme_mode.hpp"

namespace aimora::studio::themes {

QString toString(ThemeMode mode) {
    switch(mode) {
    case ThemeMode::System:
        return QStringLiteral("system");
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    }
    return QStringLiteral("system");
}

std::optional<ThemeMode> parseThemeMode(QStringView value) {
    const QString normalized = value.trimmed().toString().toLower();
    if(normalized == QStringLiteral("system")) {
        return ThemeMode::System;
    }
    if(normalized == QStringLiteral("light")) {
        return ThemeMode::Light;
    }
    if(normalized == QStringLiteral("dark")) {
        return ThemeMode::Dark;
    }
    return std::nullopt;
}

} // namespace aimora::studio::themes
