// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>

namespace aimora::studio::shell {
namespace {

constexpr int gridSpacing = 24;
constexpr int majorGridInterval = 5;

} // namespace


DrawingWorkspace::DrawingWorkspace(QWidget* parent)
    : QWidget{parent} {
    setObjectName(QStringLiteral("aimoraDrawingWorkspace"));
    setAccessibleName(tr("AIMORA drawing workspace"));
    setAccessibleDescription(
        tr("Central engineering drawing area for single-line diagrams and CAD sheets.")
    );
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setThemeTokens(tokens_);
}

void DrawingWorkspace::setThemeTokens(const themes::ThemeTokens& tokens) {
    if(!tokens.isValid()) {
        return;
    }

    tokens_ = tokens;
    QPalette workspacePalette = palette();
    workspacePalette.setColor(QPalette::Window, tokens_.canvas);
    workspacePalette.setColor(QPalette::WindowText, tokens_.textPrimary);
    setPalette(workspacePalette);
    update();
}

const themes::ThemeTokens& DrawingWorkspace::themeTokens() const noexcept {
    return tokens_;
}

QSize DrawingWorkspace::sizeHint() const {
    return QSize{1200, 760};
}

void DrawingWorkspace::paintEvent(QPaintEvent* event) {
    (void)event;

    QPainter painter{this};
    painter.fillRect(rect(), tokens_.canvas);
    painter.setRenderHint(QPainter::Antialiasing, false);

    for(int x = 0; x < width(); x += gridSpacing) {
        const bool major = ((x / gridSpacing) % majorGridInterval) == 0;
        painter.setPen(major ? tokens_.gridMajor : tokens_.gridMinor);
        painter.drawLine(x, 0, x, height());
    }
    for(int y = 0; y < height(); y += gridSpacing) {
        const bool major = ((y / gridSpacing) % majorGridInterval) == 0;
        painter.setPen(major ? tokens_.gridMajor : tokens_.gridMinor);
        painter.drawLine(0, y, width(), y);
    }

    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(tokens_.textPrimary);

    QFont headingFont = font();
    headingFont.setBold(true);
    headingFont.setPointSizeF(std::max(12.0, headingFont.pointSizeF() + 2.0));
    painter.setFont(headingFont);

    const int contentWidth = std::max(0, width() - 96);
    const int centerY = height() / 2;
    const QRect headingRect{48, centerY - 44, contentWidth, 36};
    painter.drawText(headingRect, Qt::AlignCenter, tr("AIMORA DRAWING WORKSPACE"));

    QFont detailFont = font();
    detailFont.setPointSizeF(std::max(9.0, detailFont.pointSizeF()));
    painter.setFont(detailFont);
    painter.setPen(tokens_.textSecondary);
    const QRect detailRect{48, centerY + 4, contentWidth, 52};
    painter.drawText(
        detailRect,
        Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
        tr("Semantic SLD and precision CAD rendering enters through the retained-scene packets.")
    );

    if(hasFocus()) {
        QPen focusPen{tokens_.focus};
        focusPen.setWidth(2);
        painter.setPen(focusPen);
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}

} // namespace aimora::studio::shell
