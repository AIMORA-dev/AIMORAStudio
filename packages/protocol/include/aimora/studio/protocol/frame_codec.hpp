// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/protocol/client_configuration.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace aimora::studio::protocol {

enum class FrameKind : unsigned char {
    Control = 1,
    Binary = 2,
};

struct ServiceFrame final {
    FrameKind kind{FrameKind::Control};
    QByteArray payload;
};

struct BinaryPayload final {
    QJsonObject metadata;
    QByteArray data;
};

enum class FrameDecodeStatus {
    Complete,
    NeedMoreData,
    Invalid,
    TooLarge,
};

struct FrameDecodeResult final {
    FrameDecodeStatus status{FrameDecodeStatus::NeedMoreData};
    std::optional<ServiceFrame> frame;
    QString errorCode;
    QString message;
};

[[nodiscard]] QByteArray encodeFrame(
    const ServiceFrame& frame,
    const ClientLimits& limits = ClientLimits{}
);
[[nodiscard]] FrameDecodeResult takeFrame(
    QByteArray& buffer,
    const ClientLimits& limits = ClientLimits{}
);
[[nodiscard]] QByteArray encodeControlMessage(
    const QJsonObject& object,
    const ClientLimits& limits = ClientLimits{}
);
[[nodiscard]] std::optional<QJsonObject> decodeControlMessage(
    const ServiceFrame& frame,
    QString* errorCode = nullptr,
    QString* message = nullptr
);
[[nodiscard]] QByteArray encodeBinaryPayload(
    const QJsonObject& metadata,
    const QByteArray& data
);
[[nodiscard]] std::optional<BinaryPayload> decodeBinaryPayload(
    const ServiceFrame& frame,
    QString* errorCode = nullptr,
    QString* message = nullptr
);

} // namespace aimora::studio::protocol
