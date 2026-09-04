// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"

#include <QVBoxLayout>

namespace aimora::studio::shell {

DrawingWorkspace::DrawingWorkspace(QWidget* parent) : QWidget{parent} {
    setObjectName(QStringLiteral("aimoraDrawingWorkspace"));
    setAccessibleName(tr("AIMORA drawing workspace"));
    setAccessibleDescription(
        tr("Central engineering drawing area for single-line diagrams and CAD sheets."));
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    sceneSurface_ = new renderer::SceneSurface{renderer::RendererBackend::Automatic, this};
    sceneSurface_->setObjectName(QStringLiteral("aimoraSceneSurface"));
    sceneSurface_->setAccessibleName(tr("Engineering drawing canvas"));
    sceneSurface_->setAccessibleDescription(
        tr("Retained-scene view of the current single-line diagram or CAD sheet."));
    layout->addWidget(sceneSurface_);
    setFocusProxy(sceneSurface_);
    setThemeTokens(tokens_);
}

void DrawingWorkspace::setScene(std::shared_ptr<const canvas::RetainedScene> scene) {
    sceneSurface_->setScene(std::move(scene));
}

renderer::SceneSurface* DrawingWorkspace::sceneSurface() const noexcept {
    return sceneSurface_;
}

void DrawingWorkspace::setThemeTokens(const themes::ThemeTokens& tokens) {
    if (!tokens.isValid()) {
        return;
    }

    tokens_ = tokens;
    QPalette workspacePalette = palette();
    workspacePalette.setColor(QPalette::Window, tokens_.canvas);
    workspacePalette.setColor(QPalette::WindowText, tokens_.textPrimary);
    setPalette(workspacePalette);

    sceneSurface_->setRenderPalette(canvas::RenderPalette{
        .canvas = tokens_.canvas,
        .gridMinor = tokens_.gridMinor,
        .gridMajor = tokens_.gridMajor,
        .selection = tokens_.selection,
        .diagnostic = tokens_.error,
        .text = tokens_.textPrimary,
    });
}

const themes::ThemeTokens& DrawingWorkspace::themeTokens() const noexcept {
    return tokens_;
}

QSize DrawingWorkspace::sizeHint() const {
    return QSize{1200, 760};
}

} // namespace aimora::studio::shell
