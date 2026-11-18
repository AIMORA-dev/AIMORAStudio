// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/frame_codec.hpp"

#include "aimora/studio/protocol/generated/service_protocol.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

#include <array>
#include <limits>
#include <utility>

namespace aimora::studio::protocol {
namespace {

constexpr qsizetype magicBytes = 4;
constexpr qsizetype reservedOffset = 5;
constexpr qsizetype payloadLengthOffset = 8;
constexpr qsizetype payloadLengthBytes = 4;

[[nodiscard]] qsizetype frameLimit(FrameKind kind, const ClientLimits& limits) noexcept {
    return kind == FrameKind::Control ? limits.maxControlFrameBytes : limits.maxBinaryFrameBytes;
}

void setError(QString* errorCode, QString* message, QString code, QString text) {
    if(errorCode != nullptr) {
        *errorCode = std::move(code);
    }
    if(message != nullptr) {
        *message = std::move(text);
    }
}

[[nodiscard]] bool hasValidMagic(const QByteArray& buffer) {
    return buffer.size() >= magicBytes
        && QByteArrayView{buffer}.first(magicBytes)
            == QByteArrayView{generated::frameMagic.data(), magicBytes};
}

[[nodiscard]] std::optional<FrameKind> parseFrameKind(char value) {
    const auto byte = static_cast<unsigned char>(value);
    if(byte == generated::controlFrameKind) {
        return FrameKind::Control;
    }
    if(byte == generated::binaryFrameKind) {
        return FrameKind::Binary;
    }
    return std::nullopt;
}

[[nodiscard]] quint32 readBigEndianU32(const char* bytes) noexcept {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(bytes));
}

void appendBigEndianU32(QByteArray& target, quint32 value) {
    std::array<uchar, payloadLengthBytes> bytes{};
    qToBigEndian(value, bytes.data());
    target.append(reinterpret_cast<const char*>(bytes.data()), payloadLengthBytes);
}

} // namespace

QByteArray encodeFrame(const ServiceFrame& frame, const ClientLimits& limits) {
    if(!limits.isValid() || frame.payload.size() > frameLimit(frame.kind, limits)) {
        return {};
    }
    if(frame.payload.size() > static_cast<qsizetype>(std::numeric_limits<quint32>::max())) {
        return {};
    }

    QByteArray encoded;
    encoded.reserve(generated::frameHeaderBytes + frame.payload.size());
    encoded.append(generated::frameMagic.data(), magicBytes);
    encoded.append(static_cast<char>(frame.kind));
    encoded.append(QByteArray(3, '\0'));
    appendBigEndianU32(encoded, static_cast<quint32>(frame.payload.size()));
    encoded.append(frame.payload);
    return encoded;
}

FrameDecodeResult takeFrame(QByteArray& buffer, const ClientLimits& limits) {
    if(!limits.isValid()) {
        return {
            FrameDecodeStatus::Invalid,
            std::nullopt,
            QStringLiteral("FRAME_INVALID"),
            QStringLiteral("The configured client limits are invalid."),
        };
    }
    if(buffer.size() < generated::frameHeaderBytes) {
        return {};
    }
    if(!hasValidMagic(buffer)) {
        return {
            FrameDecodeStatus::Invalid,
            std::nullopt,
            QStringLiteral("FRAME_INVALID"),
            QStringLiteral("The frame magic is invalid."),
        };
    }
    const auto kind = parseFrameKind(buffer.at(magicBytes));
    if(!kind.has_value()) {
        return {
            FrameDecodeStatus::Invalid,
            std::nullopt,
            QStringLiteral("FRAME_INVALID"),
            QStringLiteral("The frame kind is unsupported."),
        };
    }
    for(qsizetype index = reservedOffset; index < payloadLengthOffset; ++index) {
        if(buffer.at(index) != '\0') {
            return {
                FrameDecodeStatus::Invalid,
                std::nullopt,
                QStringLiteral("FRAME_INVALID"),
                QStringLiteral("Reserved frame bytes must be zero."),
            };
        }
    }

    const quint32 payloadSizeValue = readBigEndianU32(buffer.constData() + payloadLengthOffset);
    const auto payloadSize = static_cast<qsizetype>(payloadSizeValue);
    if(payloadSize > frameLimit(*kind, limits)) {
        return {
            FrameDecodeStatus::TooLarge,
            std::nullopt,
            QStringLiteral("FRAME_TOO_LARGE"),
            QStringLiteral("The frame payload exceeds the configured limit."),
        };
    }
    const qsizetype completeSize = generated::frameHeaderBytes + payloadSize;
    if(buffer.size() < completeSize) {
        return {};
    }

    ServiceFrame frame{
        *kind,
        buffer.mid(generated::frameHeaderBytes, payloadSize),
    };
    buffer.remove(0, completeSize);
    return {FrameDecodeStatus::Complete, std::move(frame), {}, {}};
}

QByteArray encodeControlMessage(const QJsonObject& object, const ClientLimits& limits) {
    const QByteArray payload = QJsonDocument{object}.toJson(QJsonDocument::Compact);
    return encodeFrame(ServiceFrame{FrameKind::Control, payload}, limits);
}

std::optional<QJsonObject> decodeControlMessage(
    const ServiceFrame& frame,
    QString* errorCode,
    QString* message
) {
    if(frame.kind != FrameKind::Control) {
        setError(
            errorCode,
            message,
            QStringLiteral("FRAME_INVALID"),
            QStringLiteral("A control frame was expected.")
        );
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(frame.payload, &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(
            errorCode,
            message,
            QStringLiteral("INVALID_REQUEST"),
            QStringLiteral("The control payload is not a valid JSON object.")
        );
        return std::nullopt;
    }
    return document.object();
}

QByteArray encodeBinaryPayload(const QJsonObject& metadata, const QByteArray& data) {
    const QByteArray metadataBytes = QJsonDocument{metadata}.toJson(QJsonDocument::Compact);
    if(metadataBytes.size() > static_cast<qsizetype>(std::numeric_limits<quint32>::max())) {
        return {};
    }
    QByteArray payload;
    payload.reserve(payloadLengthBytes + metadataBytes.size() + data.size());
    appendBigEndianU32(payload, static_cast<quint32>(metadataBytes.size()));
    payload.append(metadataBytes);
    payload.append(data);
    return payload;
}

std::optional<BinaryPayload> decodeBinaryPayload(
    const ServiceFrame& frame,
    QString* errorCode,
    QString* message
) {
    if(frame.kind != FrameKind::Binary || frame.payload.size() < payloadLengthBytes) {
        setError(
            errorCode,
            message,
            QStringLiteral("FRAME_INVALID"),
            QStringLiteral("A complete binary frame was expected.")
        );
        return std::nullopt;
    }

    const quint32 metadataSizeValue = readBigEndianU32(frame.payload.constData());
    const auto metadataSize = static_cast<qsizetype>(metadataSizeValue);
    const qsizetype metadataEnd = payloadLengthBytes + metadataSize;
    if(metadataEnd > frame.payload.size()) {
        setError(
            errorCode,
            message,
            QStringLiteral("FRAME_INVALID"),
            QStringLiteral("Binary metadata is incomplete.")
        );
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        frame.payload.mid(payloadLengthBytes, metadataSize),
        &parseError
    );
    if(parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(
            errorCode,
            message,
            QStringLiteral("FRAME_INVALID"),
            QStringLiteral("Binary metadata is not a valid JSON object.")
        );
        return std::nullopt;
    }
    return BinaryPayload{document.object(), frame.payload.mid(metadataEnd)};
}

} // namespace aimora::studio::protocol
