// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/service_process.hpp"

#include "aimora/studio/protocol/generated/service_protocol.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace aimora::studio::protocol {
namespace {

[[nodiscard]] bool isPositiveBoundedTimeout(int value) noexcept {
    return value > 0 && value <= 120000;
}

[[nodiscard]] QString protocolVersionString() {
    return QString::fromLatin1(
        generated::protocolVersion.data(),
        static_cast<qsizetype>(generated::protocolVersion.size())
    );
}

[[nodiscard]] QString cleanRoot(const QString& root) {
    const QFileInfo info{root};
    return info.exists() && info.isDir() ? info.canonicalFilePath() : QString{};
}

[[nodiscard]] bool isRecoverableTransportFailure(const QString& code) {
    return code == QStringLiteral("SERVICE_CONNECTION_FAILED")
        || code == QStringLiteral("SERVICE_WRITE_FAILED");
}

} // namespace

bool ServiceLaunchConfiguration::isValid() const {
    if(program.trimmed().isEmpty() || allowedRoots.isEmpty() || !limits.isValid()) {
        return false;
    }
    if(!isPositiveBoundedTimeout(startupTimeoutMs)
        || !isPositiveBoundedTimeout(shutdownTimeoutMs)
        || maximumAutomaticRestarts < 0
        || maximumAutomaticRestarts > 8) {
        return false;
    }
    for(const QString& root : allowedRoots) {
        if(cleanRoot(root).isEmpty()) {
            return false;
        }
    }
    if(workerProgram.trimmed().isEmpty() && !workerArguments.isEmpty()) {
        return false;
    }
    return true;
}

ServiceProcess::ServiceProcess(ServiceLaunchConfiguration configuration, QObject* parent)
    : QObject{parent}
    , configuration_{std::move(configuration)}
    , process_{new QProcess{this}}
    , startupTimer_{new QTimer{this}}
    , shutdownTimer_{new QTimer{this}} {
    startupTimer_->setSingleShot(true);
    shutdownTimer_->setSingleShot(true);
    connect(startupTimer_, &QTimer::timeout, this, [this]() {
        fail(
            QStringLiteral("SERVICE_STARTUP_TIMEOUT"),
            QStringLiteral(
                "The Julia service did not become ready and authenticate before the timeout."
            )
        );
        forceStop();
    });
    connect(shutdownTimer_, &QTimer::timeout, this, &ServiceProcess::forceStop);
    connectProcessSignals();
}

ServiceProcess::~ServiceProcess() {
    stopRequested_ = true;
    startupTimer_->stop();
    shutdownTimer_->stop();
    if(client_ != nullptr) {
        client_->close();
    }
    if(process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    cleanupSession();
}

const ServiceLaunchConfiguration& ServiceProcess::configuration() const noexcept {
    return configuration_;
}

ServiceProcess::State ServiceProcess::state() const noexcept {
    return state_;
}

ServiceClient* ServiceProcess::client() const noexcept {
    return client_;
}

QString ServiceProcess::endpoint() const {
    return endpoint_;
}

QString ServiceProcess::failureCode() const {
    return failureCode_;
}

QString ServiceProcess::failureMessage() const {
    return failureMessage_;
}

int ServiceProcess::automaticRestartCount() const noexcept {
    return automaticRestartCount_;
}

void ServiceProcess::start() {
    if(state_ != State::Stopped && state_ != State::Failed) {
        return;
    }
    if(!configuration_.isValid()) {
        fail(
            QStringLiteral("INVALID_CONFIGURATION"),
            QStringLiteral("The Julia service launch configuration is invalid.")
        );
        return;
    }
    cleanupSession();
    if(!prepareSession()) {
        return;
    }

    stopRequested_ = false;
    explicitRestartRequested_ = false;
    failureCode_.clear();
    failureMessage_.clear();
    stdoutBuffer_.clear();
    setState(State::Starting);
    process_->setProgram(configuration_.program);
    process_->setArguments(launchArguments());
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    startupTimer_->start(configuration_.startupTimeoutMs);
    process_->start(QIODevice::ReadOnly);
}

void ServiceProcess::stop() {
    stopRequested_ = true;
    explicitRestartRequested_ = false;
    if(state_ == State::Stopped) {
        return;
    }
    setState(State::Stopping);
    requestGracefulShutdown();
}

void ServiceProcess::restart() {
    explicitRestartRequested_ = true;
    stopRequested_ = true;
    if(state_ == State::Stopped || state_ == State::Failed) {
        explicitRestartRequested_ = false;
        stopRequested_ = false;
        start();
        return;
    }
    setState(State::Stopping);
    requestGracefulShutdown();
}

void ServiceProcess::setState(State state) {
    if(state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void ServiceProcess::fail(QString code, QString message) {
    failureCode_ = std::move(code);
    failureMessage_ = std::move(message);
    setState(State::Failed);
    emit failed(failureCode_, failureMessage_);
}

bool ServiceProcess::prepareSession() {
    sessionDirectory_ = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/aimora-studio-service-XXXXXX")
    );
    if(!sessionDirectory_->isValid()) {
        fail(
            QStringLiteral("SESSION_DIRECTORY_FAILED"),
            QStringLiteral("A private temporary service directory could not be created.")
        );
        return false;
    }

#ifdef Q_OS_WIN
    endpoint_ = QStringLiteral("aimora-%1-%2")
                    .arg(QCoreApplication::applicationPid())
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
#else
    endpoint_ = sessionDirectory_->filePath(QStringLiteral("service.sock"));
#endif
    sessionToken_ = createSessionToken();
    tokenFilePath_ = sessionDirectory_->filePath(QStringLiteral("session.token"));
    QFile tokenFile{tokenFilePath_};
    if(!tokenFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || tokenFile.write(sessionToken_) != static_cast<qint64>(sessionToken_.size())
        || !tokenFile.flush()) {
        fail(
            QStringLiteral("SESSION_TOKEN_FAILED"),
            QStringLiteral("The private one-use session token could not be written.")
        );
        return false;
    }
    tokenFile.close();
    if(!QFile::setPermissions(
           tokenFilePath_,
           QFileDevice::ReadOwner | QFileDevice::WriteOwner
       )) {
#ifndef Q_OS_WIN
        fail(
            QStringLiteral("SESSION_TOKEN_FAILED"),
            QStringLiteral("Private permissions could not be applied to the session token.")
        );
        return false;
#endif
    }
    return true;
}

QStringList ServiceProcess::launchArguments() const {
    QStringList arguments = configuration_.programArguments;
    arguments << QStringLiteral("--endpoint") << endpoint_;
    arguments << QStringLiteral("--token-file") << tokenFilePath_;
    for(const QString& root : configuration_.allowedRoots) {
        arguments << QStringLiteral("--allowed-root") << cleanRoot(root);
    }
    if(!configuration_.workerProgram.trimmed().isEmpty()) {
        arguments << QStringLiteral("--worker-program") << configuration_.workerProgram;
        for(const QString& argument : configuration_.workerArguments) {
            arguments << QStringLiteral("--worker-arg") << argument;
        }
    }
    arguments << QStringLiteral("--max-control-frame-bytes")
              << QString::number(configuration_.limits.maxControlFrameBytes);
    arguments << QStringLiteral("--max-binary-frame-bytes")
              << QString::number(configuration_.limits.maxBinaryFrameBytes);
    arguments << QStringLiteral("--max-pending-requests")
              << QString::number(configuration_.limits.maxPendingRequests);
    return arguments;
}

void ServiceProcess::connectProcessSignals() {
    connect(
        process_,
        &QProcess::readyReadStandardOutput,
        this,
        &ServiceProcess::processStandardOutput
    );
    connect(
        process_,
        &QProcess::readyReadStandardError,
        this,
        &ServiceProcess::processStandardError
    );
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart) {
            startupTimer_->stop();
            shutdownTimer_->stop();
            cleanupSession();
            fail(
                QStringLiteral("SERVICE_PROCESS_FAILED"),
                QStringLiteral("The Julia service process could not start.")
            );
        }
    });
    connect(
        process_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        &ServiceProcess::handleProcessFinished
    );
}

void ServiceProcess::processStandardOutput() {
    // The ready record is a small protocol version and local endpoint, not a data channel.
    constexpr qsizetype maximumReadyLineBytes = 65536;
    process_->setReadChannel(QProcess::StandardOutput);
    while(process_->bytesAvailable() > 0) {
        const QByteArray chunk = process_->read(4096);
        if(chunk.isEmpty()) {
            return;
        }
        if(state_ != State::Starting) {
            stdoutBuffer_.clear();
            continue;
        }
        stdoutBuffer_.append(chunk);
        while(state_ == State::Starting) {
            const qsizetype newline = stdoutBuffer_.indexOf('\n');
            const qsizetype lineBytes = newline < 0 ? stdoutBuffer_.size() : newline;
            if(lineBytes > maximumReadyLineBytes) {
                stdoutBuffer_.clear();
                fail(
                    QStringLiteral("SERVICE_READY_RECORD_TOO_LARGE"),
                    QStringLiteral("The service startup output exceeded the bounded line size.")
                );
                forceStop();
                return;
            }
            if(newline < 0) {
                break;
            }
            const QByteArray line = stdoutBuffer_.left(newline).trimmed();
            stdoutBuffer_.remove(0, newline + 1);
            if(line.startsWith("AIMORA_SERVICE_READY\t")) {
                handleReadyLine(line);
            }
        }
        if(state_ != State::Starting) {
            stdoutBuffer_.clear();
        }
    }
}

void ServiceProcess::processStandardError() {
    const QByteArray errorOutput = process_->readAllStandardError();
    const qsizetype marker = errorOutput.indexOf("AIMORA_SERVICE_ERROR\t");
    if(marker < 0) {
        return;
    }
    const QByteArray code = errorOutput.mid(marker + 21).split('\n').value(0).trimmed();
    if(!code.isEmpty()) {
        failureCode_ = QString::fromLatin1(code);
    }
}

void ServiceProcess::handleReadyLine(const QByteArray& line) {
    const QList<QByteArray> fields = line.split('\t');
    if(fields.size() != 3
        || QString::fromLatin1(fields.at(1)) != protocolVersionString()
        || QString::fromUtf8(fields.at(2)) != endpoint_) {
        fail(
            QStringLiteral("PROTOCOL_VERSION_UNSUPPORTED"),
            QStringLiteral("The service ready record is incompatible.")
        );
        forceStop();
        return;
    }
    setState(State::Authenticating);
    connectClient();
}

void ServiceProcess::connectClient() {
    if(client_ != nullptr) {
        client_->deleteLater();
    }
    ClientConfiguration clientConfiguration{
        .transport = LocalTransport::LocalSocket,
        .endpoint = endpoint_,
        .sessionToken = sessionToken_,
        .limits = configuration_.limits,
    };
    client_ = new ServiceClient{std::move(clientConfiguration), this};
    connect(client_, &ServiceClient::ready, this, [this]() {
        startupTimer_->stop();
        QFile::remove(tokenFilePath_);
        setState(State::Ready);
        emit ready(client_);
    });
    connect(
        client_,
        &ServiceClient::failed,
        this,
        [this](const QString& code, const QString& message) {
            if((state_ == State::Authenticating || state_ == State::Ready)
                && isRecoverableTransportFailure(code)) {
                failureCode_ = code;
                failureMessage_ = message;
                if(process_->state() != QProcess::NotRunning) {
                    process_->kill();
                }
                return;
            }
            fail(code, message);
            forceStop();
        }
    );
    client_->connectToService();
}

void ServiceProcess::requestGracefulShutdown() {
    startupTimer_->stop();
    if(client_ != nullptr && client_->isReady()) {
        const QString requestId = client_->sendRequest(generated::Method::ServiceShutdown);
        if(!requestId.isEmpty()) {
            shutdownTimer_->start(configuration_.shutdownTimeoutMs);
            return;
        }
    }
    if(process_->state() != QProcess::NotRunning) {
        process_->terminate();
        shutdownTimer_->start(configuration_.shutdownTimeoutMs);
    } else {
        handleProcessFinished(0, QProcess::NormalExit);
    }
}

void ServiceProcess::forceStop() {
    startupTimer_->stop();
    shutdownTimer_->stop();
    if(client_ != nullptr) {
        client_->close();
    }
    if(process_->state() != QProcess::NotRunning) {
        process_->kill();
    }
}

void ServiceProcess::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    startupTimer_->stop();
    shutdownTimer_->stop();
    if(client_ != nullptr) {
        client_->close();
        client_->deleteLater();
        client_ = nullptr;
    }
    cleanupSession();

    if(state_ == State::Failed && !explicitRestartRequested_) {
        return;
    }

    const bool restartExplicitly = explicitRestartRequested_;
    explicitRestartRequested_ = false;
    if(restartExplicitly) {
        stopRequested_ = false;
        setState(State::Stopped);
        QTimer::singleShot(0, this, [this]() {
            if(!stopRequested_) {
                start();
            }
        });
        return;
    }

    const bool unexpected = !stopRequested_;
    if(unexpected
        && automaticRestartCount_ < configuration_.maximumAutomaticRestarts) {
        ++automaticRestartCount_;
        setState(State::Stopped);
        emit restarted(automaticRestartCount_);
        QTimer::singleShot(100, this, [this]() {
            if(!stopRequested_) {
                start();
            }
        });
        return;
    }

    if(unexpected && state_ != State::Failed) {
        const QString exitDescription = exitStatus == QProcess::CrashExit || exitCode != 0
            ? QStringLiteral("The Julia service process crashed or returned an error.")
            : QStringLiteral("The Julia service process exited without a stop request.");
        fail(
            failureCode_.isEmpty() ? QStringLiteral("SERVICE_PROCESS_EXITED") : failureCode_,
            exitDescription
        );
        return;
    }

    stopRequested_ = false;
    setState(State::Stopped);
    emit stopped();
}

void ServiceProcess::cleanupSession() {
    if(!tokenFilePath_.isEmpty()) {
        QFile::remove(tokenFilePath_);
    }
#ifndef Q_OS_WIN
    if(!endpoint_.isEmpty()) {
        QFile::remove(endpoint_);
    }
#endif
    tokenFilePath_.clear();
    endpoint_.clear();
    sessionToken_.clear();
    sessionDirectory_.reset();
}

QByteArray ServiceProcess::createSessionToken() {
    QByteArray bytes;
    bytes.resize(32);
    QRandomGenerator generator = QRandomGenerator::securelySeeded();
    for(qsizetype index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<char>(generator.generate() & 0xffU);
    }
    return bytes.toHex();
}

} // namespace aimora::studio::protocol
