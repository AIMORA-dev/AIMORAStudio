// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/themes/theme_mode.hpp"

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

class QApplication;
class QSettings;

namespace aimora::studio::themes {

struct ThemeTokens final {
    QColor window;
    QColor panel;
    QColor panelAlternate;
    QColor canvas;
    QColor gridMinor;
    QColor gridMajor;
    QColor textPrimary;
    QColor textSecondary;
    QColor textDisabled;
    QColor border;
    QColor accent;
    QColor accentText;
    QColor selection;
    QColor focus;
    QColor warning;
    QColor error;
    QColor success;
    QColor conductor;

    [[nodiscard]] bool isValid() const noexcept;
};

[[nodiscard]] double contrastRatio(const QColor& foreground, const QColor& background) noexcept;
[[nodiscard]] ThemeTokens lightThemeTokens();
[[nodiscard]] ThemeTokens darkThemeTokens();
[[nodiscard]] QPalette makeApplicationPalette(const ThemeTokens& tokens);
[[nodiscard]] QString makeApplicationStyleSheet(const ThemeTokens& tokens);

class ThemeSettings final {
public:
    explicit ThemeSettings(QSettings& settings) noexcept;

    [[nodiscard]] ThemeMode loadMode() const;
    void saveMode(ThemeMode mode);

private:
    QSettings& settings_;
};

class ThemeController final : public QObject {
    Q_OBJECT

public:
    ThemeController(
        QApplication& application,
        ThemeSettings& settings,
        QObject* parent = nullptr
    );

    [[nodiscard]] ThemeMode requestedMode() const noexcept;
    [[nodiscard]] ThemeMode effectiveMode() const noexcept;
    [[nodiscard]] const ThemeTokens& tokens() const noexcept;

public slots:
    void setRequestedMode(ThemeMode mode);
    void refreshSystemAppearance();

signals:
    void themeChanged(ThemeMode requestedMode, ThemeMode effectiveMode);

private:
    [[nodiscard]] ThemeMode detectSystemMode() const noexcept;
    void applyTheme(bool persist);

    QApplication& application_;
    ThemeSettings& settings_;
    ThemeMode requestedMode_{ThemeMode::System};
    ThemeMode effectiveMode_{ThemeMode::Light};
    ThemeTokens tokens_{lightThemeTokens()};
};

} // namespace aimora::studio::themes
