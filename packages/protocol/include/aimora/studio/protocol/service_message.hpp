// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/protocol/generated/service_protocol.hpp"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace aimora::studio::protocol {

struct ServiceFailure final {
    QString code;
    QString message;
    QJsonObject details;

    [[nodiscard]] bool isValid() const;
};

struct ServiceResponse final {
    QString requestId;
    bool ok{false};
    QJsonObject result;
    std::optional<ServiceFailure> failure;
};

[[nodiscard]] QJsonObject makeRequest(
    QString requestId,
    generated::Method method,
    QJsonObject parameters = {}
);
[[nodiscard]] std::optional<ServiceResponse> parseResponse(
    const QJsonObject& object,
    QString* errorMessage = nullptr
);

} // namespace aimora::studio::protocol
