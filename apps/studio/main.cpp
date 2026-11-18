// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/core/application_info.hpp"
#include "aimora/studio/protocol/generated/service_protocol.hpp"
#include "aimora/studio/protocol/service_process.hpp"
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMenuBar>
#include <QSettings>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

[[nodiscard]] bool hasRawArgument(int argc, char* argv[], std::string_view argument) {
    for(int index = 1; index < argc; ++index) {
        if(std::string_view{argv[index]} == argument) {
            return true;
        }
    }
    return false;
}

int printEarlyInformation(int argc, char* argv[]) {
    using aimora::studio::core::ApplicationInfo;

    QTextStream output{stdout};
    if(hasRawArgument(argc, argv, "--architecture")) {
        output << ApplicationInfo::architectureSummary() << Qt::endl;
        return EXIT_SUCCESS;
    }
    if(hasRawArgument(argc, argv, "--version")) {
        output << ApplicationInfo::productName() << ' ' << ApplicationInfo::version()
               << Qt::endl;
        return EXIT_SUCCESS;
    }
    return -1;
}

int runServiceSmoke(
    const QString& program,
    const QStringList& programArguments,
    const QStringList& allowedRoots,
    const QString& workerProgram,
    const QStringList& workerArguments
) {
    using aimora::studio::protocol::ServiceLaunchConfiguration;
    using aimora::studio::protocol::ServiceProcess;
    namespace generated = aimora::studio::protocol::generated;

    if(program.trimmed().isEmpty()) {
        QTextStream{stderr}
            << QStringLiteral("--service-smoke requires --service-program.")
            << Qt::endl;
        return EXIT_FAILURE;
    }

    ServiceLaunchConfiguration configuration{
        .program = program,
        .programArguments = programArguments,
        .allowedRoots = allowedRoots.isEmpty()
            ? QStringList{QDir::currentPath()}
            : allowedRoots,
        .workerProgram = workerProgram,
        .workerArguments = workerArguments,
        .startupTimeoutMs = 20000,
        .shutdownTimeoutMs = 5000,
        .maximumAutomaticRestarts = 0,
    };
    ServiceProcess process{configuration};
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(30000);

    int result = EXIT_FAILURE;
    QString pingRequestId;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        QTextStream{stderr} << QStringLiteral("AIMORAService smoke timed out.") << Qt::endl;
        process.stop();
        loop.quit();
    });
    QObject::connect(
        &process,
        &ServiceProcess::failed,
        &loop,
        [&](const QString& code, const QString& message) {
            QTextStream{stderr}
                << QStringLiteral("AIMORAService smoke failed: ")
                << code << QStringLiteral(": ") << message << Qt::endl;
            loop.quit();
        }
    );
    QObject::connect(
        &process,
        &ServiceProcess::ready,
        &loop,
        [&](aimora::studio::protocol::ServiceClient* client) {
            QObject::connect(
                client,
                &aimora::studio::protocol::ServiceClient::responseReceived,
                &loop,
                [&](const QString& requestId,
                    bool ok,
                    const QJsonObject& response,
                    const QString& errorCode,
                    const QString& errorMessage) {
                    if(requestId != pingRequestId) {
                        return;
                    }
                    if(ok && response.value(QStringLiteral("nonce")).toString()
                            == QStringLiteral("aimora-studio-gui040")) {
                        QTextStream{stdout}
                            << QStringLiteral("AIMORAService authenticated smoke passed.")
                            << Qt::endl;
                        result = EXIT_SUCCESS;
                    } else {
                        QTextStream{stderr}
                            << QStringLiteral("AIMORAService ping failed: ")
                            << errorCode << QStringLiteral(": ") << errorMessage
                            << Qt::endl;
                    }
                    process.stop();
                }
            );
            pingRequestId = client->sendRequest(
                generated::Method::ServicePing,
                {{QStringLiteral("nonce"), QStringLiteral("aimora-studio-gui040")}}
            );
            if(pingRequestId.isEmpty()) {
                QTextStream{stderr}
                    << QStringLiteral("AIMORAService ping request could not be sent.")
                    << Qt::endl;
                process.stop();
            }
        }
    );
    QObject::connect(&process, &ServiceProcess::stopped, &loop, &QEventLoop::quit);

    timeout.start();
    process.start();
    loop.exec();
    timeout.stop();
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    using aimora::studio::core::ApplicationInfo;
    using aimora::studio::shell::StudioMainWindow;
    using aimora::studio::themes::ThemeController;
    using aimora::studio::themes::ThemeSettings;
    using aimora::studio::themes::parseThemeMode;

    const int earlyResult = printEarlyInformation(argc, argv);
    if(earlyResult >= 0) {
        return earlyResult;
    }

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );

    QApplication application{argc, argv};
    QCoreApplication::setApplicationName(ApplicationInfo::productName());
    QCoreApplication::setApplicationVersion(ApplicationInfo::version());
    QCoreApplication::setOrganizationName(QStringLiteral("AIMORA"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("aimora.dev"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Native AIMORAStudio drawing-first desktop shell.")
    );
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption shellSmokeOption{
        QStringList{QStringLiteral("shell-smoke")},
        QStringLiteral("Create, show, validate, and close the native shell.")
    };
    const QCommandLineOption serviceSmokeOption{
        QStringList{QStringLiteral("service-smoke")},
        QStringLiteral("Start and authenticate an AIMORAService process, then ping it.")
    };
    const QCommandLineOption serviceProgramOption{
        QStringList{QStringLiteral("service-program")},
        QStringLiteral("Executable used to start AIMORAService."),
        QStringLiteral("path")
    };
    const QCommandLineOption serviceArgumentOption{
        QStringList{QStringLiteral("service-argument")},
        QStringLiteral("Repeatable argument placed before AIMORAService session options."),
        QStringLiteral("argument")
    };
    const QCommandLineOption serviceRootOption{
        QStringList{QStringLiteral("service-root")},
        QStringLiteral("Repeatable directory accessible to AIMORAService."),
        QStringLiteral("path")
    };
    const QCommandLineOption workerProgramOption{
        QStringList{QStringLiteral("worker-program")},
        QStringLiteral("Trusted worker executable configured for the service."),
        QStringLiteral("path")
    };
    const QCommandLineOption workerArgumentOption{
        QStringList{QStringLiteral("worker-argument")},
        QStringLiteral("Repeatable argument for the trusted worker executable."),
        QStringLiteral("argument")
    };
    const QCommandLineOption resetWorkspaceOption{
        QStringList{QStringLiteral("reset-workspace")},
        QStringLiteral("Discard saved window and dock layout before startup.")
    };
    const QCommandLineOption windowedOption{
        QStringList{QStringLiteral("windowed")},
        QStringLiteral("Open in a normal window even when no saved layout exists.")
    };
    const QCommandLineOption themeOption{
        QStringList{QStringLiteral("theme")},
        QStringLiteral("Select system, light, or dark theme."),
        QStringLiteral("mode")
    };

    parser.addOption(shellSmokeOption);
    parser.addOption(serviceSmokeOption);
    parser.addOption(serviceProgramOption);
    parser.addOption(serviceArgumentOption);
    parser.addOption(serviceRootOption);
    parser.addOption(workerProgramOption);
    parser.addOption(workerArgumentOption);
    parser.addOption(resetWorkspaceOption);
    parser.addOption(windowedOption);
    parser.addOption(themeOption);
    parser.process(application);

    if(parser.isSet(serviceSmokeOption)) {
        return runServiceSmoke(
            parser.value(serviceProgramOption),
            parser.values(serviceArgumentOption),
            parser.values(serviceRootOption),
            parser.value(workerProgramOption),
            parser.values(workerArgumentOption)
        );
    }

    QSettings settings;
    if(parser.isSet(resetWorkspaceOption)) {
        settings.remove(QStringLiteral("workspace"));
        settings.sync();
    }

    ThemeSettings themeSettings{settings};
    ThemeController themeController{application, themeSettings};

    if(parser.isSet(themeOption)) {
        const auto requestedTheme = parseThemeMode(parser.value(themeOption));
        if(!requestedTheme.has_value()) {
            QTextStream{stderr}
                << QStringLiteral("Invalid theme. Use system, light, or dark.")
                << Qt::endl;
            return EXIT_FAILURE;
        }
        themeController.setRequestedMode(*requestedTheme);
    }

    StudioMainWindow window{themeController, settings};

    if(parser.isSet(shellSmokeOption)) {
        window.resize(1280, 800);
        window.show();
        application.processEvents();

        bool panelsHidden = true;
        for(const auto* panel : window.panels()) {
            panelsHidden = panelsHidden && !panel->isVisible();
        }
        const QStringList expectedMenus{
            QStringLiteral("File"),
            QStringLiteral("Edit"),
            QStringLiteral("View"),
            QStringLiteral("Draw"),
            QStringLiteral("Modify"),
            QStringLiteral("Electrical"),
            QStringLiteral("Studies"),
            QStringLiteral("Results"),
            QStringLiteral("Output"),
            QStringLiteral("Tools"),
            QStringLiteral("Help"),
        };
        const bool healthy = window.isVisible()
            && window.menuBar() != nullptr
            && window.menuTitles() == expectedMenus
            && window.centralWidget() == window.drawingWorkspace()
            && window.findChildren<QToolBar*>().isEmpty()
            && themeController.tokens().isValid()
            && panelsHidden;
        QTextStream{stdout}
            << (healthy
                    ? QStringLiteral("AIMORAStudio native shell smoke passed.")
                    : QStringLiteral("AIMORAStudio native shell smoke failed."))
            << Qt::endl;
        window.close();
        return healthy ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if(window.shouldStartMaximized() && !parser.isSet(windowedOption)) {
        window.showMaximized();
    } else {
        window.show();
    }
    return application.exec();
}
