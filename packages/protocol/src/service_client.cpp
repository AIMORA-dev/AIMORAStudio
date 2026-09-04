// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/service_client.hpp"

#include "aimora/studio/protocol/service_message.hpp"

#include <QJsonArray>
#include <QLocalSocket>
#include <QUuid>
#include <utility>

namespace aimora::studio::protocol {
namespace {

[[nodiscard]] QString protocolVersionString() {
    return QString::fromLatin1(generated::protocolVersion.data(),
                               static_cast<qsizetype>(generated::protocolVersion.size()));
}

[[nodiscard]] QString serviceVersionString() {
    return QString::fromLatin1(generated::serviceVersion.data(),
                               static_cast<qsizetype>(generated::serviceVersion.size()));
}

[[nodiscard]] QSet<QString> requiredCapabilities() {
    QSet<QString> values;
    for (const std::string_view capability : generated::capabilities) {
        values.insert(
            QString::fromLatin1(capability.data(), static_cast<qsizetype>(capability.size())));
    }
    return values;
}

} // namespace

ServiceClient::ServiceClient(ClientConfiguration configuration, QObject* parent)
    : QObject{parent}, configuration_{std::move(configuration)}, socket_{new QLocalSocket{this}} {
    connect(socket_, &QLocalSocket::connected, this, &ServiceClient::authenticate);
    connect(socket_, &QLocalSocket::readyRead, this, &ServiceClient::processInput);
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        if (state_ != State::Failed) {
            setState(State::Disconnected);
        }
        emit disconnected();
    });
    connect(socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (state_ == State::Closing || state_ == State::Disconnected) {
            return;
        }
        fail(QStringLiteral("SERVICE_CONNECTION_FAILED"), socket_->errorString());
    });
}

ServiceClient::~ServiceClient() {
    close();
}

const ClientConfiguration& ServiceClient::configuration() const noexcept {
    return configuration_;
}

ServiceClient::State ServiceClient::state() const noexcept {
    return state_;
}

bool ServiceClient::isReady() const noexcept {
    return state_ == State::Ready;
}

QString ServiceClient::failureCode() const {
    return failureCode_;
}

QString ServiceClient::failureMessage() const {
    return failureMessage_;
}

QSet<QString> ServiceClient::capabilities() const {
    return capabilities_;
}

qsizetype ServiceClient::pendingRequestCount() const noexcept {
    return pendingRequests_.size();
}

void ServiceClient::connectToService() {
    if (state_ != State::Disconnected && state_ != State::Failed) {
        return;
    }
    if (!configuration_.isValid()) {
        fail(QStringLiteral("INVALID_CONFIGURATION"),
             QStringLiteral("The local service client configuration is invalid."));
        return;
    }
    failureCode_.clear();
    failureMessage_.clear();
    inputBuffer_.clear();
    pendingRequests_.clear();
    capabilities_.clear();
    setState(State::Connecting);
    socket_->connectToServer(configuration_.endpoint, QIODevice::ReadWrite);
}

void ServiceClient::close() {
    if (state_ == State::Disconnected) {
        return;
    }
    setState(State::Closing);
    pendingRequests_.clear();
    inputBuffer_.clear();
    if (socket_->state() != QLocalSocket::UnconnectedState) {
        socket_->disconnectFromServer();
        if (socket_->state() != QLocalSocket::UnconnectedState) {
            socket_->abort();
        }
    }
    setState(State::Disconnected);
}

QString ServiceClient::sendRequest(generated::Method method, QJsonObject parameters) {
    return sendRequestInternal(method, std::move(parameters), false);
}

QString ServiceClient::cancelRequest(QString targetRequestId) {
    if (targetRequestId.trimmed().isEmpty()) {
        return {};
    }
    return sendRequest(generated::Method::RequestCancel,
                       {{QStringLiteral("target_request_id"), std::move(targetRequestId)}});
}

void ServiceClient::setState(State state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void ServiceClient::fail(QString code, QString message) {
    failureCode_ = std::move(code);
    failureMessage_ = std::move(message);
    setState(State::Failed);
    if (socket_->state() != QLocalSocket::UnconnectedState) {
        socket_->abort();
    }
    emit failed(failureCode_, failureMessage_);
}

void ServiceClient::authenticate() {
    setState(State::Authenticating);
    helloRequestId_ = sendRequestInternal(
        generated::Method::ServiceHello,
        {{QStringLiteral("token"), QString::fromUtf8(configuration_.sessionToken)}},
        true);
    if (helloRequestId_.isEmpty()) {
        fail(QStringLiteral("AUTHENTICATION_FAILED"),
             QStringLiteral("The authentication request could not be sent."));
    }
}

QString ServiceClient::sendRequestInternal(generated::Method method,
                                           QJsonObject parameters,
                                           bool allowBeforeReady) {
    const bool canSend = allowBeforeReady ? state_ == State::Authenticating : isReady();
    if (!canSend || pendingRequests_.size() >= configuration_.limits.maxPendingRequests) {
        return {};
    }

    QString requestId = nextRequestId();
    const QByteArray frame = encodeControlMessage(
        makeRequest(requestId, method, std::move(parameters)), configuration_.limits);
    if (frame.isEmpty()) {
        return {};
    }
    pendingRequests_.insert(requestId, method);
    writeBytes(frame);
    return requestId;
}

void ServiceClient::writeBytes(const QByteArray& bytes) {
    qsizetype written = 0;
    while (written < bytes.size()) {
        const qint64 count = socket_->write(bytes.constData() + written,
                                            static_cast<qint64>(bytes.size() - written));
        if (count <= 0) {
            fail(QStringLiteral("SERVICE_WRITE_FAILED"), socket_->errorString());
            return;
        }
        written += static_cast<qsizetype>(count);
    }
    socket_->flush();
}

void ServiceClient::processInput() {
    inputBuffer_.append(socket_->readAll());
    while (true) {
        const FrameDecodeResult decoded = takeFrame(inputBuffer_, configuration_.limits);
        if (decoded.status == FrameDecodeStatus::NeedMoreData) {
            return;
        }
        if (decoded.status != FrameDecodeStatus::Complete || !decoded.frame.has_value()) {
            fail(decoded.errorCode, decoded.message);
            return;
        }
        if (decoded.frame->kind == FrameKind::Control) {
            processControlFrame(*decoded.frame);
        } else {
            processBinaryFrame(*decoded.frame);
        }
        if (state_ == State::Failed) {
            return;
        }
    }
}

void ServiceClient::processControlFrame(const ServiceFrame& frame) {
    QString errorCode;
    QString message;
    const auto object = decodeControlMessage(frame, &errorCode, &message);
    if (!object.has_value()) {
        fail(std::move(errorCode), std::move(message));
        return;
    }
    const auto response = parseResponse(*object, &message);
    if (!response.has_value()) {
        fail(QStringLiteral("INVALID_RESPONSE"), std::move(message));
        return;
    }
    const auto pending = pendingRequests_.find(response->requestId);
    if (pending == pendingRequests_.end()) {
        fail(QStringLiteral("UNEXPECTED_RESPONSE"),
             QStringLiteral("The service returned an unknown request ID."));
        return;
    }
    const generated::Method method = pending.value();
    pendingRequests_.erase(pending);

    if (response->requestId == helloRequestId_) {
        if (!response->ok) {
            const ServiceFailure failure = response->failure.value_or(ServiceFailure{});
            fail(failure.code.isEmpty() ? QStringLiteral("AUTHENTICATION_FAILED") : failure.code,
                 failure.message.isEmpty() ? QStringLiteral("The service rejected authentication.")
                                           : failure.message);
            return;
        }
        completeAuthentication(response->result);
        return;
    }

    const QString failureCode = response->failure.has_value() ? response->failure->code : QString{};
    const QString failureMessage =
        response->failure.has_value() ? response->failure->message : QString{};
    emit responseReceived(
        response->requestId, response->ok, response->result, failureCode, failureMessage);

    if (method == generated::Method::ServiceShutdown && response->ok) {
        setState(State::Closing);
    }
}

void ServiceClient::processBinaryFrame(const ServiceFrame& frame) {
    QString errorCode;
    QString message;
    const auto payload = decodeBinaryPayload(frame, &errorCode, &message);
    if (!payload.has_value()) {
        fail(std::move(errorCode), std::move(message));
        return;
    }
    emit binaryPayloadReceived(payload->metadata, payload->data);
}

void ServiceClient::completeAuthentication(const QJsonObject& result) {
    if (result.value(QStringLiteral("authenticated")).toBool(false) != true ||
        result.value(QStringLiteral("protocol_version")).toString() != protocolVersionString() ||
        result.value(QStringLiteral("service_version")).toString() != serviceVersionString()) {
        fail(QStringLiteral("PROTOCOL_VERSION_UNSUPPORTED"),
             QStringLiteral("The service handshake identity is incompatible."));
        return;
    }

    const QJsonValue capabilitiesValue = result.value(QStringLiteral("capabilities"));
    if (!capabilitiesValue.isArray()) {
        fail(QStringLiteral("INVALID_RESPONSE"),
             QStringLiteral("The service handshake omitted its capabilities."));
        return;
    }
    for (const QJsonValue value : capabilitiesValue.toArray()) {
        if (!value.isString()) {
            fail(QStringLiteral("INVALID_RESPONSE"),
                 QStringLiteral("The service capability list is invalid."));
            return;
        }
        capabilities_.insert(value.toString());
    }
    const QSet<QString> required = requiredCapabilities();
    for (const QString& capability : required) {
        if (!capabilities_.contains(capability)) {
            fail(
                QStringLiteral("CAPABILITY_MISSING"),
                QStringLiteral("A required service capability is unavailable: %1").arg(capability));
            return;
        }
    }

    setState(State::Ready);
    emit ready();
}

QString ServiceClient::nextRequestId() {
    ++requestSequence_;
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QStringLiteral("studio-%1-%2").arg(requestSequence_).arg(uuid);
}

} // namespace aimora::studio::protocol
