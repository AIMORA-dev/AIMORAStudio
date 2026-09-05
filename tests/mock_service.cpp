// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/frame_codec.hpp"
#include "aimora/studio/protocol/generated/service_protocol.hpp"
#include "aimora/studio/protocol/service_message.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTextStream>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace {

using aimora::studio::protocol::ClientLimits;
using aimora::studio::protocol::decodeControlMessage;
using aimora::studio::protocol::encodeBinaryPayload;
using aimora::studio::protocol::encodeControlMessage;
using aimora::studio::protocol::encodeFrame;
using aimora::studio::protocol::FrameDecodeStatus;
using aimora::studio::protocol::FrameKind;
using aimora::studio::protocol::ServiceFrame;
using aimora::studio::protocol::takeFrame;
namespace generated = aimora::studio::protocol::generated;

[[nodiscard]] QString protocolVersion() {
    return QString::fromLatin1(generated::protocolVersion.data(),
                               static_cast<qsizetype>(generated::protocolVersion.size()));
}

[[nodiscard]] QString serviceVersion() {
    return QString::fromLatin1(generated::serviceVersion.data(),
                               static_cast<qsizetype>(generated::serviceVersion.size()));
}

[[nodiscard]] QJsonArray capabilityArray() {
    QJsonArray values;
    for (const std::string_view capability : generated::capabilities) {
        values.append(
            QString::fromLatin1(capability.data(), static_cast<qsizetype>(capability.size())));
    }
    return values;
}

[[nodiscard]] QJsonObject success(QString requestId, QJsonObject result) {
    return {
        {QStringLiteral("protocol_version"), protocolVersion()},
        {QStringLiteral("request_id"), std::move(requestId)},
        {QStringLiteral("ok"), true},
        {QStringLiteral("result"), std::move(result)},
    };
}

[[nodiscard]] QJsonObject failure(QString requestId,
                                  QString code,
                                  QString message,
                                  QJsonObject details = {}) {
    return {
        {QStringLiteral("protocol_version"), protocolVersion()},
        {QStringLiteral("request_id"), std::move(requestId)},
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"),
         QJsonObject{
             {QStringLiteral("code"), std::move(code)},
             {QStringLiteral("message"), std::move(message)},
             {QStringLiteral("details"), std::move(details)},
         }},
    };
}

class MockConnection final : public QObject {
  public:
    MockConnection(QLocalSocket* socket,
                   QByteArray token,
                   bool rejectAuthentication,
                   bool ignoreAuthentication,
                   QString crashOnceFile,
                   QObject* parent)
        : QObject{parent}, socket_{socket}, token_{std::move(token)},
          crashOnceFile_{std::move(crashOnceFile)}, rejectAuthentication_{rejectAuthentication},
          ignoreAuthentication_{ignoreAuthentication} {
        socket_->setParent(this);
        connect(socket_, &QLocalSocket::readyRead, this, [this]() { processInput(); });
    }

  private:
    void processInput() {
        input_.append(socket_->readAll());
        while (true) {
            const auto decoded = takeFrame(input_, limits_);
            if (decoded.status == FrameDecodeStatus::NeedMoreData) {
                return;
            }
            if (decoded.status != FrameDecodeStatus::Complete || !decoded.frame.has_value()) {
                socket_->disconnectFromServer();
                return;
            }
            const auto object = decodeControlMessage(*decoded.frame);
            if (!object.has_value()) {
                socket_->disconnectFromServer();
                return;
            }
            handleRequest(*object);
        }
    }

    void handleRequest(const QJsonObject& request) {
        const QString requestId = request.value(QStringLiteral("request_id")).toString();
        const QString method = request.value(QStringLiteral("method")).toString();
        const QJsonObject parameters = request.value(QStringLiteral("params")).toObject();
        if (request.value(QStringLiteral("protocol_version")).toString() != protocolVersion()) {
            writeControl(failure(requestId,
                                 QStringLiteral("PROTOCOL_VERSION_UNSUPPORTED"),
                                 QStringLiteral("Protocol mismatch.")));
            return;
        }
        if (!authenticated_) {
            if (ignoreAuthentication_ && method == QStringLiteral("service.hello")) {
                return;
            }
            const bool accepted =
                method == QStringLiteral("service.hello") &&
                parameters.value(QStringLiteral("token")).toString().toUtf8() == token_ &&
                !rejectAuthentication_;
            if (!accepted) {
                writeControl(failure(requestId,
                                     QStringLiteral("AUTHENTICATION_FAILED"),
                                     QStringLiteral("Authentication failed.")));
                return;
            }
            authenticated_ = true;
            writeControl(success(requestId,
                                 {
                                     {QStringLiteral("protocol_version"), protocolVersion()},
                                     {QStringLiteral("service_version"), serviceVersion()},
                                     {QStringLiteral("authenticated"), true},
                                     {QStringLiteral("capabilities"), capabilityArray()},
                                 }));
            return;
        }

        if (method == QStringLiteral("project.open")) {
            drawingOpen_ = true;
            writeControl(success(requestId,
                {{QStringLiteral("project_id"), QStringLiteral("project.drafting")},
                 {QStringLiteral("display_name"), QStringLiteral("Drafting fixture")},
                 {QStringLiteral("revision"), QString::number(drawingRevision_)},
                 {QStringLiteral("drawing_view_id"), QStringLiteral("view.drafting")},
                 {QStringLiteral("drawing_layer_id"), QStringLiteral("layer.drafting")},
                 {QStringLiteral("modified"), drawingRevision_ != savedDrawingRevision_},
                 {QStringLiteral("can_save"), true},
                 {QStringLiteral("edit_operations"), QJsonArray{QStringLiteral("draw.line")}},
                 {QStringLiteral("drawing_scene"), drawingScene()}}));
        } else if (method == QStringLiteral("semantic.commit") || method == QStringLiteral("project.save")) {
            if (!drawingOpen_ || parameters.value(QStringLiteral("project_id")).toString() != QStringLiteral("project.drafting")) {
                writeControl(failure(requestId, QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Drawing is not open.")));
                return;
            }
            if (parameters.value(QStringLiteral("base_revision")).toString() != QString::number(drawingRevision_)) {
                writeControl(failure(requestId, QStringLiteral("REVISION_CONFLICT"), QStringLiteral("Drawing revision conflict.")));
                return;
            }
            if (method == QStringLiteral("project.save")) {
                savedDrawingRevision_ = drawingRevision_;
                writeControl(success(requestId,
                    {{QStringLiteral("project_id"), QStringLiteral("project.drafting")},
                     {QStringLiteral("revision"), QString::number(drawingRevision_)},
                     {QStringLiteral("saved"), true}, {QStringLiteral("modified"), false}}));
                return;
            }
            const QJsonArray points = parameters.value(QStringLiteral("points")).toArray();
            if (parameters.value(QStringLiteral("operation")).toString() != QStringLiteral("draw.line") ||
                points.size() != 2 || points[0] == points[1]) {
                writeControl(failure(requestId, QStringLiteral("INVALID_REQUEST"), QStringLiteral("Fixture accepts only a two-point line.")));
                return;
            }
            ++drawingRevision_;
            drawingItems_.append(QJsonObject{
                {QStringLiteral("item_id"), QString::number(drawingRevision_)},
                {QStringLiteral("owner_id"), QStringLiteral("drawing.line.%1").arg(drawingRevision_)},
                {QStringLiteral("kind"), QStringLiteral("line")}, {QStringLiteral("points"), points}});
            writeControl(success(requestId,
                {{QStringLiteral("status"), QStringLiteral("accepted")},
                 {QStringLiteral("revision"), QString::number(drawingRevision_)},
                 {QStringLiteral("drawing_scene"), drawingScene()}}));
        } else if (method == QStringLiteral("service.capabilities")) {
            writeControl(success(requestId,
                                 {
                                     {QStringLiteral("protocol_version"), protocolVersion()},
                                     {QStringLiteral("service_version"), serviceVersion()},
                                     {QStringLiteral("capabilities"), capabilityArray()},
                                 }));
        } else if (method == QStringLiteral("service.ping")) {
            if (!crashOnceFile_.isEmpty() && !QFile::exists(crashOnceFile_)) {
                QFile marker{crashOnceFile_};
                if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                    marker.write("crashed\n") != 8 || !marker.flush()) {
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                marker.close();
                std::abort();
            }
            writeControl(
                success(requestId,
                        {
                            {QStringLiteral("nonce"), parameters.value(QStringLiteral("nonce"))},
                        }));
        } else if (method == QStringLiteral("request.cancel")) {
            writeControl(success(requestId,
                                 {
                                     {QStringLiteral("target_request_id"),
                                      parameters.value(QStringLiteral("target_request_id"))},
                                     {QStringLiteral("cancelled"), true},
                                 }));
        } else if (method == QStringLiteral("inspector.describe")) {
            QJsonObject identity = parameters;
            identity.insert(QStringLiteral("equipment_class"), QStringLiteral("breaker"));
            identity.insert(QStringLiteral("result_bindings"), QJsonArray{});
            writeControl(success(
                requestId,
                {
                    {QStringLiteral("schema_version"), QStringLiteral("1.0.0")},
                    {QStringLiteral("revision"), QString::number(inspectionRevision_)},
                    {QStringLiteral("identity"), identity},
                    {QStringLiteral("sections"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("id"), QStringLiteral("ratings")},
                         {QStringLiteral("title"), QStringLiteral("Ratings")},
                         {QStringLiteral("available"), true},
                         {QStringLiteral("fields"),
                          QJsonArray{QJsonObject{
                              {QStringLiteral("path"), QStringLiteral("ratings.voltage")},
                              {QStringLiteral("label"), QStringLiteral("Rated voltage")},
                              {QStringLiteral("kind"), QStringLiteral("number")},
                              {QStringLiteral("canonical_unit"), QStringLiteral("V")},
                              {QStringLiteral("display_units"), QJsonArray{}},
                              {QStringLiteral("choices"), QJsonArray{}},
                              {QStringLiteral("dependencies"), QJsonArray{}},
                          }}},
                     }}},
                    {QStringLiteral("values"),
                     QJsonObject{
                         {QStringLiteral("ratings.voltage"),
                          QJsonObject{{QStringLiteral("value"), 11000.0},
                                      {QStringLiteral("canonical_unit"), QStringLiteral("V")},
                                      {QStringLiteral("issues"), QJsonArray{}}}},
                     }},
                    {QStringLiteral("undo_available"), inspectionRevision_ > 7},
                    {QStringLiteral("redo_available"), false},
                }));
        } else if (method == QStringLiteral("inspector.commit")) {
            if (parameters.value(QStringLiteral("base_revision")).toString() !=
                QString::number(inspectionRevision_)) {
                writeControl(failure(
                    requestId,
                    QStringLiteral("REVISION_CONFLICT"),
                    QStringLiteral("Inspector revision conflict."),
                    {{QStringLiteral("status"), QStringLiteral("conflict")},
                     {QStringLiteral("revision"), QString::number(inspectionRevision_)},
                     {QStringLiteral("issues"), QJsonArray{}}}));
            } else {
                ++inspectionRevision_;
                writeControl(success(
                    requestId,
                    {{QStringLiteral("status"), QStringLiteral("accepted")},
                     {QStringLiteral("base_revision"),
                      parameters.value(QStringLiteral("base_revision"))},
                     {QStringLiteral("revision"), QString::number(inspectionRevision_)},
                     {QStringLiteral("issues"), QJsonArray{}},
                     {QStringLiteral("affected_model_paths"),
                      QJsonArray{QStringLiteral("asset.breaker.1")}},
                     {QStringLiteral("affected_view_ids"),
                      QJsonArray{QStringLiteral("view.sld.1")}},
                     {QStringLiteral("invalidated_result_ids"), QJsonArray{}}}));
            }
        } else if (method == QStringLiteral("inspector.undo") ||
                   method == QStringLiteral("inspector.redo")) {
            ++inspectionRevision_;
            writeControl(success(
                requestId,
                {{QStringLiteral("status"), QStringLiteral("accepted")},
                 {QStringLiteral("revision"), QString::number(inspectionRevision_)},
                 {QStringLiteral("issues"), QJsonArray{}}}));
        } else if (method == QStringLiteral("worker.start")) {
            workerRunning_ = true;
            writeControl(success(requestId,
                                 {
                                     {QStringLiteral("worker_id"), QStringLiteral("worker-test")},
                                     {QStringLiteral("state"), QStringLiteral("running")},
                                 }));
        } else if (method == QStringLiteral("worker.status")) {
            writeControl(success(
                requestId,
                {
                    {QStringLiteral("worker_id"), QStringLiteral("worker-test")},
                    {QStringLiteral("state"),
                     workerRunning_ ? QStringLiteral("running") : QStringLiteral("stopped")},
                }));
        } else if (method == QStringLiteral("worker.stop")) {
            workerRunning_ = false;
            writeControl(success(requestId,
                                 {
                                     {QStringLiteral("worker_id"), QStringLiteral("worker-test")},
                                     {QStringLiteral("state"), QStringLiteral("stopped")},
                                 }));
        } else if (method == QStringLiteral("result.window")) {
            const QByteArray data{"AIMORA"};
            writeControl(success(requestId,
                                 {
                                     {QStringLiteral("binary_frame"), true},
                                     {QStringLiteral("length"), data.size()},
                                 }));
            const QJsonObject metadata{
                {QStringLiteral("dtype"), QStringLiteral("uint8")},
                {QStringLiteral("units"), QStringLiteral("byte")},
                {QStringLiteral("length"), data.size()},
            };
            const QByteArray payload = encodeBinaryPayload(metadata, data);
            socket_->write(encodeFrame(ServiceFrame{FrameKind::Binary, payload}, limits_));
            socket_->flush();
        } else if (method == QStringLiteral("service.shutdown")) {
            writeControl(success(requestId, {{QStringLiteral("accepted"), true}}));
            socket_->flush();
            QCoreApplication::quit();
        } else {
            writeControl(failure(requestId,
                                 QStringLiteral("METHOD_NOT_FOUND"),
                                 QStringLiteral("Method not found.")));
        }
    }

    QJsonObject drawingScene() const {
        return {{QStringLiteral("items"), drawingItems_},
                {QStringLiteral("unsupported_owner_ids"), QJsonArray{}}};
    }

    void writeControl(const QJsonObject& object) {
        socket_->write(encodeControlMessage(object, limits_));
        socket_->flush();
    }

    QLocalSocket* socket_{nullptr};
    QByteArray token_;
    QByteArray input_;
    ClientLimits limits_;
    QString crashOnceFile_;
    bool rejectAuthentication_{false};
    bool ignoreAuthentication_{false};
    bool authenticated_{false};
    bool workerRunning_{false};
    quint64 inspectionRevision_{7};
    bool drawingOpen_{false};
    quint64 drawingRevision_{1};
    quint64 savedDrawingRevision_{1};
    QJsonArray drawingItems_;
};

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("endpoint"), {}, QStringLiteral("name")});
    parser.addOption({QStringLiteral("token-file"), {}, QStringLiteral("path")});
    parser.addOption({QStringLiteral("allowed-root"), {}, QStringLiteral("path")});
    parser.addOption({QStringLiteral("worker-program"), {}, QStringLiteral("path")});
    parser.addOption({QStringLiteral("worker-arg"), {}, QStringLiteral("value")});
    parser.addOption({QStringLiteral("max-control-frame-bytes"), {}, QStringLiteral("n")});
    parser.addOption({QStringLiteral("max-binary-frame-bytes"), {}, QStringLiteral("n")});
    parser.addOption({QStringLiteral("max-pending-requests"), {}, QStringLiteral("n")});
    parser.addOption(QCommandLineOption{QStringLiteral("mock-reject-auth")});
    parser.addOption(QCommandLineOption{QStringLiteral("mock-ignore-auth")});
    parser.addOption({QStringLiteral("mock-crash-once-file"), {}, QStringLiteral("path")});
    parser.process(application);

    const QString endpoint = parser.value(QStringLiteral("endpoint"));
    const QString tokenFilePath = parser.value(QStringLiteral("token-file"));
    QFile tokenFile{tokenFilePath};
    if (endpoint.isEmpty() || !tokenFile.open(QIODevice::ReadOnly)) {
        return EXIT_FAILURE;
    }
    const QByteArray token = tokenFile.readAll().trimmed();
    tokenFile.close();
    QFile::remove(tokenFilePath);

#ifndef Q_OS_WIN
    QLocalServer::removeServer(endpoint);
#endif
    QLocalServer server;
    if (!server.listen(endpoint)) {
        return EXIT_FAILURE;
    }
    QObject::connect(&server, &QLocalServer::newConnection, &application, [&]() {
        while (server.hasPendingConnections()) {
            new MockConnection{
                server.nextPendingConnection(),
                token,
                parser.isSet(QStringLiteral("mock-reject-auth")),
                parser.isSet(QStringLiteral("mock-ignore-auth")),
                parser.value(QStringLiteral("mock-crash-once-file")),
                &application,
            };
        }
    });

    QTextStream{stdout} << QStringLiteral("AIMORA_SERVICE_READY\t") << protocolVersion()
                        << QLatin1Char('\t') << endpoint << Qt::endl;
    return application.exec();
}
