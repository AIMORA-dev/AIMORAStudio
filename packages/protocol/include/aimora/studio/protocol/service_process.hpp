// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/protocol/client_configuration.hpp"
#include "aimora/studio/protocol/service_client.hpp"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <memory>

class QTemporaryDir;
class QTimer;

namespace aimora::studio::protocol {

struct ServiceLaunchConfiguration final {
    QString program;
    QStringList programArguments;
    QStringList allowedRoots;
    QString workerProgram;
    QStringList workerArguments;
    ClientLimits limits;
    int startupTimeoutMs{15000};
    int shutdownTimeoutMs{3000};
    int maximumAutomaticRestarts{1};

    [[nodiscard]] bool isValid() const;
};

class ServiceProcess final : public QObject {
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Starting,
        Authenticating,
        Ready,
        Stopping,
        Failed,
    };
    Q_ENUM(State)

    explicit ServiceProcess(ServiceLaunchConfiguration configuration, QObject* parent = nullptr);
    ~ServiceProcess() override;

    [[nodiscard]] const ServiceLaunchConfiguration& configuration() const noexcept;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] ServiceClient* client() const noexcept;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString failureCode() const;
    [[nodiscard]] QString failureMessage() const;
    [[nodiscard]] int automaticRestartCount() const noexcept;

    void start();
    void stop();
    void restart();

signals:
    void stateChanged(aimora::studio::protocol::ServiceProcess::State state);
    void ready(aimora::studio::protocol::ServiceClient* client);
    void stopped();
    void restarted(int attempt);
    void failed(const QString& code, const QString& message);

private:
    void setState(State state);
    void fail(QString code, QString message);
    [[nodiscard]] bool prepareSession();
    [[nodiscard]] QStringList launchArguments() const;
    void connectProcessSignals();
    void processStandardOutput();
    void processStandardError();
    void handleReadyLine(const QByteArray& line);
    void connectClient();
    void requestGracefulShutdown();
    void forceStop();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void cleanupSession();
    [[nodiscard]] static QByteArray createSessionToken();

    ServiceLaunchConfiguration configuration_;
    QProcess* process_{nullptr};
    QTimer* startupTimer_{nullptr};
    QTimer* shutdownTimer_{nullptr};
    std::unique_ptr<QTemporaryDir> sessionDirectory_;
    ServiceClient* client_{nullptr};
    QByteArray stdoutBuffer_;
    QString endpoint_;
    QString tokenFilePath_;
    QByteArray sessionToken_;
    QString failureCode_;
    QString failureMessage_;
    int automaticRestartCount_{0};
    bool stopRequested_{false};
    bool explicitRestartRequested_{false};
    State state_{State::Stopped};
};

} // namespace aimora::studio::protocol
