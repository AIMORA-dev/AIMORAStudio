// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/themes/theme_system.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

#include <algorithm>
#include <array>
#include <cmath>

namespace aimora::studio::themes {
namespace {

constexpr double minimumTextContrast = 4.5;

[[nodiscard]] double linearizedChannel(double channel) noexcept {
    if(channel <= 0.04045) {
        return channel / 12.92;
    }
    return std::pow((channel + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double relativeLuminance(const QColor& color) noexcept {
    return (0.2126 * linearizedChannel(color.redF()))
        + (0.7152 * linearizedChannel(color.greenF()))
        + (0.0722 * linearizedChannel(color.blueF()));
}

[[nodiscard]] QString cssColor(const QColor& color) {
    return color.name(QColor::HexRgb);
}

} // namespace

bool ThemeTokens::isValid() const noexcept {
    const std::array<const QColor*, 18> colors{
        &window,
        &panel,
        &panelAlternate,
        &canvas,
        &gridMinor,
        &gridMajor,
        &textPrimary,
        &textSecondary,
        &textDisabled,
        &border,
        &accent,
        &accentText,
        &selection,
        &focus,
        &warning,
        &error,
        &success,
        &conductor,
    };

    const bool colorsAreOpaque = std::all_of(
        colors.cbegin(),
        colors.cend(),
        [](const QColor* color) { return color->isValid() && color->alpha() == 255; }
    );
    if(!colorsAreOpaque) {
        return false;
    }

    return contrastRatio(textPrimary, window) >= minimumTextContrast
        && contrastRatio(textPrimary, panel) >= minimumTextContrast
        && contrastRatio(textPrimary, canvas) >= minimumTextContrast
        && contrastRatio(textSecondary, window) >= minimumTextContrast
        && contrastRatio(accentText, accent) >= minimumTextContrast;
}

double contrastRatio(const QColor& foreground, const QColor& background) noexcept {
    if(!foreground.isValid() || !background.isValid()) {
        return 0.0;
    }

    const double first = relativeLuminance(foreground);
    const double second = relativeLuminance(background);
    const double lighter = std::max(first, second);
    const double darker = std::min(first, second);
    return (lighter + 0.05) / (darker + 0.05);
}

ThemeTokens lightThemeTokens() {
    return ThemeTokens{
        .window = QColor{QStringLiteral("#F4F7FA")},
        .panel = QColor{QStringLiteral("#FFFFFF")},
        .panelAlternate = QColor{QStringLiteral("#EAF0F6")},
        .canvas = QColor{QStringLiteral("#FAFCFE")},
        .gridMinor = QColor{QStringLiteral("#E2E8F0")},
        .gridMajor = QColor{QStringLiteral("#C4CFDC")},
        .textPrimary = QColor{QStringLiteral("#17212B")},
        .textSecondary = QColor{QStringLiteral("#425466")},
        .textDisabled = QColor{QStringLiteral("#7B8794")},
        .border = QColor{QStringLiteral("#C7D1DC")},
        .accent = QColor{QStringLiteral("#075FCE")},
        .accentText = QColor{QStringLiteral("#FFFFFF")},
        .selection = QColor{QStringLiteral("#C7E0FF")},
        .focus = QColor{QStringLiteral("#075FCE")},
        .warning = QColor{QStringLiteral("#8A5D00")},
        .error = QColor{QStringLiteral("#B42318")},
        .success = QColor{QStringLiteral("#067647")},
        .conductor = QColor{QStringLiteral("#1F2937")},
    };
}

ThemeTokens darkThemeTokens() {
    return ThemeTokens{
        .window = QColor{QStringLiteral("#141A21")},
        .panel = QColor{QStringLiteral("#1B232D")},
        .panelAlternate = QColor{QStringLiteral("#222C37")},
        .canvas = QColor{QStringLiteral("#0F151B")},
        .gridMinor = QColor{QStringLiteral("#25313D")},
        .gridMajor = QColor{QStringLiteral("#36485A")},
        .textPrimary = QColor{QStringLiteral("#F1F5F9")},
        .textSecondary = QColor{QStringLiteral("#C7D0DA")},
        .textDisabled = QColor{QStringLiteral("#818D9B")},
        .border = QColor{QStringLiteral("#3A4959")},
        .accent = QColor{QStringLiteral("#61A8FF")},
        .accentText = QColor{QStringLiteral("#07111D")},
        .selection = QColor{QStringLiteral("#244E79")},
        .focus = QColor{QStringLiteral("#7AB8FF")},
        .warning = QColor{QStringLiteral("#F4C152")},
        .error = QColor{QStringLiteral("#FF817A")},
        .success = QColor{QStringLiteral("#62D6A5")},
        .conductor = QColor{QStringLiteral("#E6EDF3")},
    };
}

QPalette makeApplicationPalette(const ThemeTokens& tokens) {
    QPalette palette;
    palette.setColor(QPalette::Window, tokens.window);
    palette.setColor(QPalette::WindowText, tokens.textPrimary);
    palette.setColor(QPalette::Base, tokens.panel);
    palette.setColor(QPalette::AlternateBase, tokens.panelAlternate);
    palette.setColor(QPalette::ToolTipBase, tokens.panel);
    palette.setColor(QPalette::ToolTipText, tokens.textPrimary);
    palette.setColor(QPalette::Text, tokens.textPrimary);
    palette.setColor(QPalette::Button, tokens.panel);
    palette.setColor(QPalette::ButtonText, tokens.textPrimary);
    palette.setColor(QPalette::BrightText, tokens.error);
    palette.setColor(QPalette::Highlight, tokens.accent);
    palette.setColor(QPalette::HighlightedText, tokens.accentText);
    palette.setColor(QPalette::PlaceholderText, tokens.textSecondary);
    palette.setColor(QPalette::Link, tokens.accent);
    palette.setColor(QPalette::LinkVisited, tokens.focus);
    palette.setColor(QPalette::Light, tokens.panelAlternate);
    palette.setColor(QPalette::Midlight, tokens.border);
    palette.setColor(QPalette::Mid, tokens.border);
    palette.setColor(QPalette::Dark, tokens.gridMajor);
    palette.setColor(QPalette::Shadow, tokens.canvas);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, tokens.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, tokens.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, tokens.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, tokens.panelAlternate);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, tokens.textDisabled);
    return palette;
}

QString makeApplicationStyleSheet(const ThemeTokens& tokens) {
    return QStringLiteral(
               "QToolTip { color: %1; background-color: %2; border: 1px solid %3; "
               "padding: 4px; }"
               "QMenuBar::item:selected, QMenu::item:selected { "
               "background-color: %4; color: %1; }"
               "QDockWidget::title { background-color: %5; color: %1; "
               "border-bottom: 1px solid %3; padding: 6px; }"
               "QWidget#aimoraDrawingWorkspace { background-color: %6; }"
               "QWidget[aimoraPanel=\"true\"] { background-color: %2; }"
           )
        .arg(
            cssColor(tokens.textPrimary),
            cssColor(tokens.panel),
            cssColor(tokens.border),
            cssColor(tokens.selection),
            cssColor(tokens.panelAlternate),
            cssColor(tokens.canvas)
        );
}

ThemeSettings::ThemeSettings(QSettings& settings) noexcept
    : settings_{settings} {}

ThemeMode ThemeSettings::loadMode() const {
    const QString stored = settings_.value(
        QStringLiteral("appearance/theme"),
        toString(ThemeMode::System)
    ).toString();
    return parseThemeMode(stored).value_or(ThemeMode::System);
}

void ThemeSettings::saveMode(ThemeMode mode) {
    settings_.setValue(QStringLiteral("appearance/theme"), toString(mode));
    settings_.sync();
}

ThemeController::ThemeController(
    QApplication& application,
    ThemeSettings& settings,
    QObject* parent
)
    : QObject{parent},
      application_{application},
      settings_{settings},
      requestedMode_{settings_.loadMode()} {
    connect(
        QGuiApplication::styleHints(),
        &QStyleHints::colorSchemeChanged,
        this,
        [this](Qt::ColorScheme) {
            if(requestedMode_ == ThemeMode::System) {
                refreshSystemAppearance();
            }
        }
    );
    applyTheme(false);
}

ThemeMode ThemeController::requestedMode() const noexcept {
    return requestedMode_;
}

ThemeMode ThemeController::effectiveMode() const noexcept {
    return effectiveMode_;
}

const ThemeTokens& ThemeController::tokens() const noexcept {
    return tokens_;
}

void ThemeController::setRequestedMode(ThemeMode mode) {
    requestedMode_ = mode;
    applyTheme(true);
}

void ThemeController::refreshSystemAppearance() {
    applyTheme(false);
}

ThemeMode ThemeController::detectSystemMode() const noexcept {
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark ? ThemeMode::Dark : ThemeMode::Light;
}

void ThemeController::applyTheme(bool persist) {
    if(persist) {
        settings_.saveMode(requestedMode_);
    }

    const ThemeMode nextEffective = requestedMode_ == ThemeMode::System
        ? detectSystemMode()
        : requestedMode_;
    const ThemeTokens nextTokens = nextEffective == ThemeMode::Dark
        ? darkThemeTokens()
        : lightThemeTokens();

    const bool changed = nextEffective != effectiveMode_
        || nextTokens.canvas != tokens_.canvas
        || nextTokens.window != tokens_.window;
    effectiveMode_ = nextEffective;
    tokens_ = nextTokens;

    application_.setPalette(makeApplicationPalette(tokens_));
    application_.setStyleSheet(makeApplicationStyleSheet(tokens_));

    if(changed || persist) {
        emit themeChanged(requestedMode_, effectiveMode_);
    }
}

} // namespace aimora::studio::themes
