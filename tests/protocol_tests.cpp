// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/frame_codec.hpp"
#include "aimora/studio/protocol/generated/service_protocol.hpp"
#include "aimora/studio/protocol/service_client.hpp"
#include "aimora/studio/protocol/service_message.hpp"
#include "aimora/studio/protocol/service_process.hpp"

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace generated = aimora::studio::protocol::generated;
using aimora::studio::protocol::ClientLimits;
using aimora::studio::protocol::FrameDecodeStatus;
using aimora::studio::protocol::FrameKind;
using aimora::studio::protocol::ServiceFrame;
using aimora::studio::protocol::ServiceLaunchConfiguration;
using aimora::studio::protocol::ServiceProcess;
using aimora::studio::protocol::decodeBinaryPayload;
using aimora::studio::protocol::decodeControlMessage;
using aimora::studio::protocol::encodeBinaryPayload;
using aimora::studio::protocol::encodeControlMessage;
using aimora::studio::protocol::encodeFrame;
using aimora::studio::protocol::makeRequest;
using aimora::studio::protocol::parseResponse;
using aimora::studio::protocol::takeFrame;

class ProtocolTests final : public QObject {
    Q_OBJECT

private slots:
    void generatedBindingsMatchCanonicalSchema();
    void controlFramesRoundTripIncrementally();
    void malformedAndOversizedFramesAreRejected();
    void binaryPayloadRoundTrips();
    void responseEnvelopeIsStrict();
    void serviceProcessAuthenticatesAndSupportsLifecycle();
    void serviceProcessRejectsAuthenticationFailure();
    void serviceProcessTimesOutDuringAuthentication();
    void serviceProcessRecoversAfterCrash();
};

void ProtocolTests::generatedBindingsMatchCanonicalSchema() {
    QCOMPARE(
        QString::fromLatin1(
            generated::protocolVersion.data(),
            static_cast<qsizetype>(generated::protocolVersion.size())
        ),
        QStringLiteral("1.0")
    );
    QCOMPARE(
        QString::fromLatin1(
            generated::serviceVersion.data(),
            static_cast<qsizetype>(generated::serviceVersion.size())
        ),
        QStringLiteral("0.1.0")
    );
    QCOMPARE(
        QString::fromLatin1(
            generated::schemaSha256.data(),
            static_cast<qsizetype>(generated::schemaSha256.size())
        ),
        QStringLiteral("ebf63b6e991532dc1fa7b32f660d6050708a8dd03243b03ce95ae98a2a02c8a8")
    );
    QCOMPARE(generated::frameHeaderBytes, 12);
    QCOMPARE(generated::methodName(generated::Method::ServiceHello),
             QStringLiteral("service.hello"));
    QVERIFY(generated::parseMethod(QStringView{u"result.window"}).has_value());
    QVERIFY(!generated::parseMethod(QStringView{u"private.solver"}).has_value());
}

void ProtocolTests::controlFramesRoundTripIncrementally() {
    ClientLimits limits;
    const QJsonObject request = makeRequest(
        QStringLiteral("test-1"),
        generated::Method::ServicePing,
        {{QStringLiteral("nonce"), QStringLiteral("abc")}}
    );
    const QByteArray encoded = encodeControlMessage(request, limits);
    QVERIFY(!encoded.isEmpty());

    QByteArray buffer = encoded.left(7);
    QCOMPARE(takeFrame(buffer, limits).status, FrameDecodeStatus::NeedMoreData);
    buffer.append(encoded.mid(7));
    const auto decoded = takeFrame(buffer, limits);
    QCOMPARE(decoded.status, FrameDecodeStatus::Complete);
    QVERIFY(decoded.frame.has_value());
    QCOMPARE(decoded.frame->kind, FrameKind::Control);
    const auto object = decodeControlMessage(*decoded.frame);
    QVERIFY(object.has_value());
    QCOMPARE(*object, request);
    QVERIFY(buffer.isEmpty());
}

void ProtocolTests::malformedAndOversizedFramesAreRejected() {
    ClientLimits limits;
    QByteArray malformed(12, '\0');
    malformed.replace(0, 4, QByteArrayLiteral("BAD!"));
    QCOMPARE(takeFrame(malformed, limits).status, FrameDecodeStatus::Invalid);

    ServiceFrame frame{FrameKind::Control, QByteArray(limits.maxControlFrameBytes + 1, 'x')};
    QVERIFY(encodeFrame(frame, limits).isEmpty());

    QByteArray reserved = encodeControlMessage(QJsonObject{}, limits);
    QVERIFY(!reserved.isEmpty());
    reserved[6] = 1;
    QCOMPARE(takeFrame(reserved, limits).status, FrameDecodeStatus::Invalid);
}

void ProtocolTests::binaryPayloadRoundTrips() {
    const QJsonObject metadata{
        {QStringLiteral("dtype"), QStringLiteral("float64")},
        {QStringLiteral("units"), QStringLiteral("V")},
        {QStringLiteral("length"), 4},
    };
    const QByteArray data = QByteArray::fromHex("00010203");
    const QByteArray payload = encodeBinaryPayload(metadata, data);
    const ServiceFrame frame{FrameKind::Binary, payload};
    const auto decoded = decodeBinaryPayload(frame);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->metadata, metadata);
    QCOMPARE(decoded->data, data);
}

void ProtocolTests::responseEnvelopeIsStrict() {
    const QJsonObject valid{
        {QStringLiteral("protocol_version"), QStringLiteral("1.0")},
        {QStringLiteral("request_id"), QStringLiteral("test-2")},
        {QStringLiteral("ok"), true},
        {QStringLiteral("result"), QJsonObject{{QStringLiteral("value"), 1}}},
    };
    const auto parsed = parseResponse(valid);
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->ok);
    QCOMPARE(parsed->requestId, QStringLiteral("test-2"));

    QJsonObject invalid = valid;
    invalid.insert(QStringLiteral("protocol_version"), QStringLiteral("9.9"));
    QVERIFY(!parseResponse(invalid).has_value());
}

void ProtocolTests::serviceProcessAuthenticatesAndSupportsLifecycle() {
    const QString mockService = qEnvironmentVariable("AIMORA_MOCK_SERVICE_PATH");
    QVERIFY2(!mockService.isEmpty(), "AIMORA_MOCK_SERVICE_PATH is required");
    QVERIFY(QFile::exists(mockService));

    QTemporaryDir allowedRoot;
    QVERIFY(allowedRoot.isValid());
    ServiceLaunchConfiguration configuration{
        .program = mockService,
        .programArguments = {},
        .allowedRoots = {allowedRoot.path()},
        .workerProgram = {},
        .workerArguments = {},
        .limits = {},
        .startupTimeoutMs = 5000,
        .shutdownTimeoutMs = 2000,
        .maximumAutomaticRestarts = 0,
    };
    ServiceProcess process{configuration};
    QSignalSpy failureSpy{&process, &ServiceProcess::failed};
    QSignalSpy readySpy{&process, &ServiceProcess::ready};
    process.start();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Ready, 5000);
    QCOMPARE(failureSpy.count(), 0);
    QVERIFY(process.client() != nullptr);
    QVERIFY(process.client()->isReady());
    QCOMPARE(readySpy.count(), 1);

    QSignalSpy responseSpy{
        process.client(),
        &aimora::studio::protocol::ServiceClient::responseReceived
    };
    const QString pingId = process.client()->sendRequest(
        generated::Method::ServicePing,
        {{QStringLiteral("nonce"), QStringLiteral("round-trip")}}
    );
    QVERIFY(!pingId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 3000);
    const QList<QVariant> pingArguments = responseSpy.takeFirst();
    QCOMPARE(pingArguments.at(0).toString(), pingId);
    QVERIFY(pingArguments.at(1).toBool());
    QCOMPARE(
        pingArguments.at(2).toJsonObject().value(QStringLiteral("nonce")).toString(),
        QStringLiteral("round-trip")
    );

    const QString cancelId = process.client()->cancelRequest(QStringLiteral("target-1"));
    QVERIFY(!cancelId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 3000);
    const QList<QVariant> cancelArguments = responseSpy.takeFirst();
    QCOMPARE(cancelArguments.at(0).toString(), cancelId);
    QVERIFY(cancelArguments.at(1).toBool());
    QCOMPARE(
        cancelArguments.at(2)
            .toJsonObject()
            .value(QStringLiteral("target_request_id"))
            .toString(),
        QStringLiteral("target-1")
    );

    const QString workerId = process.client()->sendRequest(generated::Method::WorkerStart);
    QVERIFY(!workerId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 3000);
    QVERIFY(responseSpy.takeFirst().at(1).toBool());

    QSignalSpy binarySpy{
        process.client(),
        &aimora::studio::protocol::ServiceClient::binaryPayloadReceived
    };
    const QString windowId = process.client()->sendRequest(
        generated::Method::ResultWindow,
        {{QStringLiteral("artifact_id"), QStringLiteral("test")},
         {QStringLiteral("offset"), 0},
         {QStringLiteral("length"), 6}}
    );
    QVERIFY(!windowId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(binarySpy.count(), 1, 3000);
    QCOMPARE(binarySpy.takeFirst().at(1).toByteArray(), QByteArrayLiteral("AIMORA"));

    process.restart();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Ready, 5000);
    QCOMPARE(readySpy.count(), 2);

    process.stop();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Stopped, 5000);
}

void ProtocolTests::serviceProcessRejectsAuthenticationFailure() {
    const QString mockService = qEnvironmentVariable("AIMORA_MOCK_SERVICE_PATH");
    QVERIFY(!mockService.isEmpty());
    QTemporaryDir allowedRoot;
    QVERIFY(allowedRoot.isValid());
    ServiceLaunchConfiguration configuration{
        .program = mockService,
        .programArguments = {QStringLiteral("--mock-reject-auth")},
        .allowedRoots = {allowedRoot.path()},
        .workerProgram = {},
        .workerArguments = {},
        .limits = {},
        .startupTimeoutMs = 5000,
        .shutdownTimeoutMs = 1000,
        .maximumAutomaticRestarts = 0,
    };
    ServiceProcess process{configuration};
    process.start();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Failed, 5000);
    QCOMPARE(process.failureCode(), QStringLiteral("AUTHENTICATION_FAILED"));
}

void ProtocolTests::serviceProcessTimesOutDuringAuthentication() {
    const QString mockService = qEnvironmentVariable("AIMORA_MOCK_SERVICE_PATH");
    QVERIFY(!mockService.isEmpty());
    QTemporaryDir allowedRoot;
    QVERIFY(allowedRoot.isValid());
    ServiceLaunchConfiguration configuration{
        .program = mockService,
        .programArguments = {QStringLiteral("--mock-ignore-auth")},
        .allowedRoots = {allowedRoot.path()},
        .workerProgram = {},
        .workerArguments = {},
        .limits = {},
        .startupTimeoutMs = 250,
        .shutdownTimeoutMs = 1000,
        .maximumAutomaticRestarts = 0,
    };
    ServiceProcess process{configuration};
    QSignalSpy failureSpy{&process, &ServiceProcess::failed};
    process.start();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Failed, 3000);
    QCOMPARE(process.failureCode(), QStringLiteral("SERVICE_STARTUP_TIMEOUT"));
    QCOMPARE(failureSpy.count(), 1);
}

void ProtocolTests::serviceProcessRecoversAfterCrash() {
    const QString mockService = qEnvironmentVariable("AIMORA_MOCK_SERVICE_PATH");
    QVERIFY(!mockService.isEmpty());
    QTemporaryDir allowedRoot;
    QTemporaryDir recoveryState;
    QVERIFY(allowedRoot.isValid());
    QVERIFY(recoveryState.isValid());
    const QString crashMarker = recoveryState.filePath(QStringLiteral("crashed-once"));
    ServiceLaunchConfiguration configuration{
        .program = mockService,
        .programArguments = {
            QStringLiteral("--mock-crash-once-file"),
            crashMarker,
        },
        .allowedRoots = {allowedRoot.path()},
        .workerProgram = {},
        .workerArguments = {},
        .limits = {},
        .startupTimeoutMs = 5000,
        .shutdownTimeoutMs = 2000,
        .maximumAutomaticRestarts = 1,
    };
    ServiceProcess process{configuration};
    QSignalSpy failureSpy{&process, &ServiceProcess::failed};
    QSignalSpy readySpy{&process, &ServiceProcess::ready};
    QSignalSpy restartSpy{&process, &ServiceProcess::restarted};
    process.start();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Ready, 5000);
    QCOMPARE(readySpy.count(), 1);

    const QString crashingPing = process.client()->sendRequest(
        generated::Method::ServicePing,
        {{QStringLiteral("nonce"), QStringLiteral("crash-once")}}
    );
    QVERIFY(!crashingPing.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(restartSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 2, 5000);
    QCOMPARE(process.state(), ServiceProcess::State::Ready);
    QCOMPARE(process.automaticRestartCount(), 1);
    QCOMPARE(failureSpy.count(), 0);
    QVERIFY(QFile::exists(crashMarker));

    QSignalSpy responseSpy{
        process.client(),
        &aimora::studio::protocol::ServiceClient::responseReceived
    };
    const QString recoveredPing = process.client()->sendRequest(
        generated::Method::ServicePing,
        {{QStringLiteral("nonce"), QStringLiteral("recovered")}}
    );
    QVERIFY(!recoveredPing.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 3000);
    const QList<QVariant> responseArguments = responseSpy.takeFirst();
    QCOMPARE(responseArguments.at(0).toString(), recoveredPing);
    QVERIFY(responseArguments.at(1).toBool());
    QCOMPARE(
        responseArguments.at(2).toJsonObject().value(QStringLiteral("nonce")).toString(),
        QStringLiteral("recovered")
    );

    process.stop();
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Stopped, 5000);
}

QTEST_GUILESS_MAIN(ProtocolTests)

#include "protocol_tests.moc"
