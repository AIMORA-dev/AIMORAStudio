// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/protocol/client_configuration.hpp"
#include "aimora/studio/protocol/frame_codec.hpp"
#include "aimora/studio/protocol/generated/service_protocol.hpp"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

class QLocalSocket;

namespace aimora::studio::protocol {

class ServiceClient final : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Authenticating,
        Ready,
        Closing,
        Failed,
    };
    Q_ENUM(State)

    explicit ServiceClient(ClientConfiguration configuration, QObject* parent = nullptr);
    ~ServiceClient() override;

    [[nodiscard]] const ClientConfiguration& configuration() const noexcept;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] QString failureCode() const;
    [[nodiscard]] QString failureMessage() const;
    [[nodiscard]] QSet<QString> capabilities() const;
    [[nodiscard]] qsizetype pendingRequestCount() const noexcept;

    void connectToService();
    void close();
    [[nodiscard]] QString sendRequest(
        generated::Method method,
        QJsonObject parameters = {}
    );
    [[nodiscard]] QString cancelRequest(QString targetRequestId);

signals:
    void stateChanged(aimora::studio::protocol::ServiceClient::State state);
    void ready();
    void responseReceived(
        const QString& requestId,
        bool ok,
        const QJsonObject& result,
        const QString& errorCode,
        const QString& errorMessage
    );
    void binaryPayloadReceived(const QJsonObject& metadata, const QByteArray& data);
    void failed(const QString& code, const QString& message);
    void disconnected();

private:
    void setState(State state);
    void fail(QString code, QString message);
    void authenticate();
    [[nodiscard]] QString sendRequestInternal(
        generated::Method method,
        QJsonObject parameters,
        bool allowBeforeReady
    );
    void writeBytes(const QByteArray& bytes);
    void processInput();
    void processControlFrame(const ServiceFrame& frame);
    void processBinaryFrame(const ServiceFrame& frame);
    void completeAuthentication(const QJsonObject& result);
    [[nodiscard]] QString nextRequestId();

    ClientConfiguration configuration_;
    QLocalSocket* socket_{nullptr};
    QByteArray inputBuffer_;
    QHash<QString, generated::Method> pendingRequests_;
    QSet<QString> capabilities_;
    QString helloRequestId_;
    QString failureCode_;
    QString failureMessage_;
    quint64 requestSequence_{0};
    State state_{State::Disconnected};
};

} // namespace aimora::studio::protocol
