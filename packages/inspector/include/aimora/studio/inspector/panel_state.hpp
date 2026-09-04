// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QSize>
#include <cstdint>

namespace aimora::studio::inspector {

enum class PanelPresentation : std::uint8_t {
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
