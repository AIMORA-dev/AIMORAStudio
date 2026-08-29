// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QByteArray>
#include <QString>
#include <QtTypes>

namespace aimora::studio::protocol {

enum class LocalTransport {
    LocalSocket,
    NamedPipe,
};

struct ClientLimits final {
    qsizetype maxControlFrameBytes{1024 * 1024};
    qsizetype maxBinaryFrameBytes{64 * 1024 * 1024};
    qsizetype maxPendingRequests{128};

    [[nodiscard]] bool isValid() const noexcept;
};

struct ClientConfiguration final {
    LocalTransport transport{LocalTransport::LocalSocket};
    QString endpoint;
    QByteArray sessionToken;
    ClientLimits limits;

    [[nodiscard]] bool isValid() const;
};

} // namespace aimora::studio::protocol
