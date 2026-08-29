// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/client_configuration.hpp"

namespace aimora::studio::protocol {

bool ClientLimits::isValid() const noexcept {
    return maxControlFrameBytes > 0 && maxBinaryFrameBytes >= maxControlFrameBytes
        && maxPendingRequests > 0;
}

bool ClientConfiguration::isValid() const {
    constexpr qsizetype minimumSessionTokenBytes = 32;
    return !endpoint.trimmed().isEmpty() && sessionToken.size() >= minimumSessionTokenBytes
        && limits.isValid();
}

} // namespace aimora::studio::protocol
