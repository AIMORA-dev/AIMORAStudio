// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/service_message.hpp"

#include <utility>

namespace aimora::studio::protocol {
namespace {

[[nodiscard]] QString protocolVersionString() {
    return QString::fromLatin1(
        generated::protocolVersion.data(),
        static_cast<qsizetype>(generated::protocolVersion.size())
    );
}

void setParseError(QString* errorMessage, QString message) {
    if(errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

} // namespace

bool ServiceFailure::isValid() const {
    return !code.trimmed().isEmpty() && !message.trimmed().isEmpty();
}

QJsonObject makeRequest(
    QString requestId,
    generated::Method method,
    QJsonObject parameters
) {
    return {
        {QStringLiteral("protocol_version"), protocolVersionString()},
        {QStringLiteral("request_id"), std::move(requestId)},
        {QStringLiteral("method"), generated::methodName(method)},
        {QStringLiteral("params"), std::move(parameters)},
    };
}

std::optional<ServiceResponse> parseResponse(
    const QJsonObject& object,
    QString* errorMessage
) {
    const QJsonValue protocolValue = object.value(QStringLiteral("protocol_version"));
    const QJsonValue requestIdValue = object.value(QStringLiteral("request_id"));
    const QJsonValue okValue = object.value(QStringLiteral("ok"));
    if(!protocolValue.isString() || !requestIdValue.isString() || !okValue.isBool()) {
        setParseError(errorMessage, QStringLiteral("The response envelope is incomplete."));
        return std::nullopt;
    }
    if(protocolValue.toString() != protocolVersionString()) {
        setParseError(
            errorMessage,
            QStringLiteral("The response protocol version is unsupported.")
        );
        return std::nullopt;
    }
    const QString requestId = requestIdValue.toString();
    if(requestId.trimmed().isEmpty()) {
        setParseError(errorMessage, QStringLiteral("The response request ID is empty."));
        return std::nullopt;
    }

    ServiceResponse response;
    response.requestId = requestId;
    response.ok = okValue.toBool();
    if(response.ok) {
        const QJsonValue resultValue = object.value(QStringLiteral("result"));
        if(!resultValue.isObject()) {
            setParseError(
                errorMessage,
                QStringLiteral("A successful response requires an object result.")
            );
            return std::nullopt;
        }
        response.result = resultValue.toObject();
        return response;
    }

    const QJsonValue errorValue = object.value(QStringLiteral("error"));
    if(!errorValue.isObject()) {
        setParseError(errorMessage, QStringLiteral("A failed response requires an error object."));
        return std::nullopt;
    }
    const QJsonObject errorObject = errorValue.toObject();
    const QJsonValue codeValue = errorObject.value(QStringLiteral("code"));
    const QJsonValue messageValue = errorObject.value(QStringLiteral("message"));
    const QJsonValue detailsValue = errorObject.value(QStringLiteral("details"));
    if(!codeValue.isString() || !messageValue.isString()) {
        setParseError(errorMessage, QStringLiteral("The response error is incomplete."));
        return std::nullopt;
    }

    ServiceFailure failure{
        codeValue.toString(),
        messageValue.toString(),
        detailsValue.isObject() ? detailsValue.toObject() : QJsonObject{},
    };
    if(!failure.isValid()) {
        setParseError(errorMessage, QStringLiteral("The response error is invalid."));
        return std::nullopt;
    }
    response.failure = std::move(failure);
    return response;
}

} // namespace aimora::studio::protocol
