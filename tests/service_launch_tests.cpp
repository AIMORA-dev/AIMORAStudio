// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/protocol/service_process.hpp"

#include <QDir>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cstdio>

using aimora::studio::protocol::ServiceLaunchConfiguration;
using aimora::studio::protocol::ServiceProcess;

class ServiceLaunchTests final : public QObject {
    Q_OBJECT

  private slots:
    void stopCancelsQueuedAutomaticRestart() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ServiceLaunchConfiguration configuration;
        configuration.program = QCoreApplication::applicationFilePath();
        configuration.programArguments = QStringList{QStringLiteral("--exit-service-without-ready")};
        configuration.allowedRoots = QStringList{root.path()};
        configuration.startupTimeoutMs = 5000;
        configuration.maximumAutomaticRestarts = 1;
        ServiceProcess process{configuration};
        QSignalSpy restarts{&process, &ServiceProcess::restarted};
        QSignalSpy failures{&process, &ServiceProcess::failed};
        QSignalSpy states{&process, &ServiceProcess::stateChanged};
        connect(&process, &ServiceProcess::restarted, &process, &ServiceProcess::stop);
        process.start();
        QTRY_COMPARE_WITH_TIMEOUT(restarts.count(), 1, 4000);

        QTimer restartDeadline;
        restartDeadline.setSingleShot(true);
        QSignalSpy elapsed{&restartDeadline, &QTimer::timeout};
        restartDeadline.start(250);
        QTRY_COMPARE_WITH_TIMEOUT(elapsed.count(), 1, 2000);
        QCOMPARE(process.state(), ServiceProcess::State::Stopped);
        QCOMPARE(failures.count(), 0);
        QVERIFY(process.endpoint().isEmpty());
        qsizetype starts = 0;
        for(const QList<QVariant>& change : states) {
            if(qvariant_cast<ServiceProcess::State>(change.at(0)) == ServiceProcess::State::Starting) {
                ++starts;
            }
        }
        QCOMPARE(starts, 1);
    }

    void oversizedStartupOutputIsRejected_data() {
        QTest::addColumn<bool>("terminated");
        QTest::newRow("unterminated-output") << false;
        QTest::newRow("terminated-output") << true;
    }

    void oversizedStartupOutputIsRejected() {
        QFETCH(bool, terminated);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ServiceLaunchConfiguration configuration;
        configuration.program = QCoreApplication::applicationFilePath();
        configuration.programArguments = QStringList{QStringLiteral("--oversized-startup-output")};
        if(terminated) {
            configuration.programArguments.append(QStringLiteral("--terminate-output-line"));
        }
        configuration.allowedRoots = QStringList{root.path()};
        configuration.startupTimeoutMs = 5000;
        configuration.maximumAutomaticRestarts = 0;
        QVERIFY(configuration.isValid());
        ServiceProcess process{configuration};
        QSignalSpy failures{&process, &ServiceProcess::failed};
        process.start();
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Failed, 4000);
        QCOMPARE(process.failureCode(), QStringLiteral("SERVICE_READY_RECORD_TOO_LARGE"));
        QTRY_VERIFY_WITH_TIMEOUT(process.endpoint().isEmpty(), 4000);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(process.automaticRestartCount(), 0);
    }

    void unusedSessionCleanupIsSilent() {
        QTest::failOnWarning(QRegularExpression{QStringLiteral("QFile::remove:.*")});
        ServiceProcess process{ServiceLaunchConfiguration{}};
        process.stop();
        process.stop();
        QCOMPARE(process.state(), ServiceProcess::State::Stopped);
        QVERIFY(process.endpoint().isEmpty());
    }

    void failedLaunchPreservesErrorAndReleasesSession() {
        QTest::failOnWarning(QRegularExpression{QStringLiteral("QFile::remove:.*")});
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ServiceLaunchConfiguration configuration;
        configuration.program = QDir{root.path()}.filePath(QStringLiteral("missing-service"));
        configuration.allowedRoots = QStringList{root.path()};
        configuration.startupTimeoutMs = 250;
        configuration.maximumAutomaticRestarts = 0;
        QVERIFY(configuration.isValid());

        ServiceProcess process{configuration};
        QSignalSpy failures{&process, &ServiceProcess::failed};
        process.start();
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Failed, 2000);
        QCOMPARE(process.failureCode(), QStringLiteral("SERVICE_PROCESS_FAILED"));
        QVERIFY(process.endpoint().isEmpty());
        QCOMPARE(failures.count(), 1);

        // Let the original startup deadline elapse: it must not replace the launch error.
        QTimer deadline;
        deadline.setSingleShot(true);
        QSignalSpy elapsed{&deadline, &QTimer::timeout};
        deadline.start(configuration.startupTimeoutMs + 100);
        QTRY_COMPARE_WITH_TIMEOUT(elapsed.count(), 1, 2000);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(process.failureCode(), QStringLiteral("SERVICE_PROCESS_FAILED"));

        process.start();
        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 2, 2000);
        QCOMPARE(process.state(), ServiceProcess::State::Failed);
        QVERIFY(process.endpoint().isEmpty());
        process.stop();
        QCOMPARE(process.state(), ServiceProcess::State::Stopped);
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    if(application.arguments().contains(QStringLiteral("--exit-service-without-ready"))) {
        return 0;
    }
    if(application.arguments().contains(QStringLiteral("--oversized-startup-output"))) {
        QByteArray output(65537, 'x');
        if(application.arguments().contains(QStringLiteral("--terminate-output-line"))) {
            output.append('\n');
        }
        const auto outputSize = static_cast<std::size_t>(output.size());
        if(std::fwrite(output.constData(), 1, outputSize, stdout) != outputSize
            || std::fflush(stdout) != 0) {
            return 1;
        }
        return application.exec();
    }
    ServiceLaunchTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "service_launch_tests.moc"
