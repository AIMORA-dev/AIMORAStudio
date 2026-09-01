// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QSize>

namespace aimora::studio::inspector {

enum class PanelPresentation {
    Hidden,
    Overlay,
    Docked,
    Floating,
};

struct PanelState final {
    PanelPresentation presentation{PanelPresentation::Hidden};
    bool pinned{false};
    QSize preferredSize{420, 720};

    [[nodiscard]] bool isVisible() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
};

} // namespace aimora::studio::inspector
