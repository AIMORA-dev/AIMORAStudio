// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/client_configuration.hpp"

namespace aimora::studio::protocol {

bool ClientLimits::isValid() const noexcept {
    return maxControlFrameBytes > 12
        && maxControlFrameBytes <= 16 * 1024 * 1024
        && maxBinaryFrameBytes >= maxControlFrameBytes
        && maxBinaryFrameBytes <= 512 * 1024 * 1024
        && maxPendingRequests > 0
        && maxPendingRequests <= 4096
        && maxPathBytes >= 256
        && maxPathBytes <= 32 * 1024
        && maxWindowBytes > 0
        && maxWindowBytes <= maxBinaryFrameBytes
        && maxWindowBytes <= 64 * 1024 * 1024
        && maxWorkers > 0
        && maxWorkers <= 64;
}

bool ClientConfiguration::isValid() const {
    constexpr qsizetype minimumSessionTokenBytes = 64;
    constexpr qsizetype maximumSessionTokenBytes = 256;
    const QByteArray endpointBytes = endpoint.trimmed().toUtf8();
    return !endpointBytes.isEmpty()
        && endpointBytes.size() <= limits.maxPathBytes
        && sessionToken.size() >= minimumSessionTokenBytes
        && sessionToken.size() <= maximumSessionTokenBytes
        && limits.isValid();
}

} // namespace aimora::studio::protocol
