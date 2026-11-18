// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/inspector/panel_state.hpp"

namespace aimora::studio::inspector {

bool PanelState::isVisible() const noexcept {
    return presentation != PanelPresentation::Hidden;
}

bool PanelState::isValid() const noexcept {
    if(!preferredSize.isValid() || preferredSize.isEmpty()) {
        return false;
    }
    return isVisible() || !pinned;
}

} // namespace aimora::studio::inspector
