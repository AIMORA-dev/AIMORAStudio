// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/core/application_info.hpp"
#include "aimora/studio/protocol/generated/service_protocol.hpp"
#include "aimora/studio/protocol/service_process.hpp"
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/shell/local_service_configuration.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QApplication>
#include <QAction>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QGuiApplication>
#include <QMenuBar>
#include <QMenu>
#include <QSettings>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <memory>
#include <optional>
#include <utility>

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
        .limits = {},
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
    QString phase = QStringLiteral("startup");
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        result = EXIT_FAILURE;
        QTextStream{stderr} << QStringLiteral("AIMORAService smoke timed out during ")
            << phase << QStringLiteral(" after ") << elapsed.elapsed()
            << QStringLiteral(" ms.") << Qt::endl;
        process.stop();
        loop.quit();
    });
    QObject::connect(
        &process,
        &ServiceProcess::failed,
        &loop,
        [&](const QString& code, const QString& message) {
            result = EXIT_FAILURE;
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
            phase = QStringLiteral("ping response");
            QTextStream{stdout} << QStringLiteral("AIMORAService authenticated after ")
                << elapsed.elapsed() << QStringLiteral(" ms.") << Qt::endl;
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
                    QTextStream{stdout} << QStringLiteral("AIMORAService ping response after ")
                        << elapsed.elapsed() << QStringLiteral(" ms.") << Qt::endl;
                    if(ok && response.value(QStringLiteral("nonce")).toString()
                            == QStringLiteral("aimora-studio-service-smoke")) {
                        QTextStream{stdout}
                            << QStringLiteral("AIMORAService ping accepted; waiting for shutdown.")
                            << Qt::endl;
                        result = EXIT_SUCCESS;
                    } else {
                        QTextStream{stderr}
                            << QStringLiteral("AIMORAService ping failed: ")
                            << errorCode << QStringLiteral(": ") << errorMessage
                            << Qt::endl;
                    }
                    phase = QStringLiteral("shutdown");
                    process.stop();
                }
            );
            pingRequestId = client->sendRequest(
                generated::Method::ServicePing,
                {{QStringLiteral("nonce"), QStringLiteral("aimora-studio-service-smoke")}}
            );
            if(pingRequestId.isEmpty()) {
                QTextStream{stderr}
                    << QStringLiteral("AIMORAService ping request could not be sent.")
                    << Qt::endl;
                process.stop();
            }
        }
    );
    QObject::connect(&process, &ServiceProcess::stopped, &loop, [&]() {
        if(result == EXIT_SUCCESS) {
            QTextStream{stdout} << QStringLiteral("AIMORAService authenticated smoke passed after ")
                << elapsed.elapsed() << QStringLiteral(" ms.") << Qt::endl;
        }
        loop.quit();
    });

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
    parser.addPositionalArgument(QStringLiteral("project"),
        QStringLiteral("Canonical AIMORA project to open after startup."),
        QStringLiteral("[project]"));
    parser.process(application);

    if(parser.positionalArguments().size() > 1 ||
       (!parser.positionalArguments().isEmpty() &&
        (parser.isSet(shellSmokeOption) || parser.isSet(serviceSmokeOption)))) {
        QTextStream{stderr}
            << QStringLiteral("Supply at most one project file; smoke checks do not open projects.")
            << Qt::endl;
        return EXIT_FAILURE;
    }

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
    if(!parser.isSet(shellSmokeOption)) {
        for(QAction* menuAction : window.menuBar()->actions()) {
            if(menuAction->menu() &&
               menuAction->text().remove(QLatin1Char('&')) == QObject::tr("Tools")) {
                QAction* setup = menuAction->menu()->addAction(QObject::tr("Configure AIMORA Service..."));
                setup->setObjectName(QStringLiteral("aimora.configure-local-service"));
                setup->setStatusTip(QObject::tr("Choose trusted Julia and service paths for the next Studio launch."));
                QObject::connect(setup, &QAction::triggered, &window, [&]() {
                    if(parser.isSet(serviceProgramOption)) {
                        QMessageBox::information(&window, QObject::tr("AIMORA Service setup"),
                            QObject::tr("This launch uses explicit service command-line options. "
                                        "Change those options to select a different service."));
                        return;
                    }
                    if(aimora::studio::shell::configureLocalDrawingService(window, settings, true)) {
                        QMessageBox::information(&window, QObject::tr("AIMORA Service setup"),
                            QObject::tr("Service paths saved. They will be used on the next Studio launch. "
                                        "Your current project and service have not been changed."));
                    }
                });
                break;
            }
        }
    }
    QString pendingProjectPath;
    bool pendingProjectCreation = false;
    QString initialProjectPath = parser.positionalArguments().value(0);
    std::unique_ptr<aimora::studio::protocol::ServiceProcess> projectService;
    if (!parser.isSet(shellSmokeOption)) {
        aimora::studio::protocol::ServiceLaunchConfiguration configuration;
        configuration.program = parser.value(serviceProgramOption);
        configuration.programArguments = parser.values(serviceArgumentOption);
        configuration.allowedRoots = parser.values(serviceRootOption);
        if (configuration.allowedRoots.isEmpty()) {
            configuration.allowedRoots = {QDir::currentPath()};
        }
        configuration.workerProgram = parser.value(workerProgramOption);
        configuration.workerArguments = parser.values(workerArgumentOption);
        QAction* openAction = window.commandAction(QStringView{u"file.open"});
        openAction->setEnabled(true);
        openAction->setStatusTip(QObject::tr("Open a canonical AIMORA drawing project."));
        const auto openOrCreateDrawing = [&, configuration](bool create) {
            if (!pendingProjectPath.isEmpty()) {
                return;
            }
            if (window.isWindowModified()) {
                QMessageBox::warning(&window, QObject::tr("Unsaved Drawing"),
                    QObject::tr("Save the current drawing before creating or opening another project."));
                return;
            }
            QString path = create ? QFileDialog::getSaveFileName(
                &window, QObject::tr("New AIMORA Drawing"), {}, QObject::tr("AIMORA projects (*.aimora.yaml)"),
                nullptr, QFileDialog::DontConfirmOverwrite) : initialProjectPath.isEmpty()
                ? QFileDialog::getOpenFileName(
                    &window, QObject::tr("Open AIMORA Project"), {},
                    QObject::tr("AIMORA projects (*.aimora.yaml);;All files (*)"))
                : std::exchange(initialProjectPath, {});
            if (path.isEmpty()) {
                return;
            }
            if (create && !path.endsWith(QStringLiteral(".aimora.yaml"), Qt::CaseInsensitive)) {
                path += QStringLiteral(".aimora.yaml");
            }
            const QFileInfo projectFile{path};
            if (create && (projectFile.exists() || projectFile.isSymLink())) {
                QMessageBox::warning(&window, QObject::tr("New Drawing"),
                    QObject::tr("Choose a new filename. New Drawing does not replace existing files."));
                return;
            }
            if (!create && !projectFile.isFile()) {
                QMessageBox::warning(&window, QObject::tr("Open Project"),
                                     QObject::tr("The selected project file does not exist or is not a file."));
                return;
            }
            path = projectFile.absoluteFilePath();
            if (!projectService) {
                auto launch = configuration;
                if (launch.program.isEmpty()) {
                    const auto selected = aimora::studio::shell::configureLocalDrawingService(window, settings);
                    if (!selected.has_value()) {
                        return;
                    }
                    launch = *selected;
                    // The user explicitly chose this project file. Limit this local
                    // service session to its directory rather than the whole workspace.
                    launch.allowedRoots = {QFileInfo{path}.absolutePath()};
                }
                projectService = std::make_unique<aimora::studio::protocol::ServiceProcess>(launch);
                QObject::connect(projectService.get(), &aimora::studio::protocol::ServiceProcess::ready,
                                 &window, [&](aimora::studio::protocol::ServiceClient* client) {
                    window.bindProjectService(client);
                    const QString pending = std::exchange(pendingProjectPath, {});
                    const bool createPending = std::exchange(pendingProjectCreation, false);
                    const bool sent = pending.isEmpty() || (createPending ?
                        window.createDrawingProject(pending, QFileInfo{pending}.fileName().chopped(12)) :
                        window.openDrawingProject(pending));
                    if (!sent) {
                        QMessageBox::warning(&window, QObject::tr("Open Project"),
                                             QObject::tr("The drawing open request could not be sent."));
                    }
                });
                QObject::connect(projectService.get(), &aimora::studio::protocol::ServiceProcess::failed,
                                 &window, [&](const QString&, const QString& message) {
                    pendingProjectPath.clear();
                    pendingProjectCreation = false;
                    window.bindProjectService(nullptr);
                    QMessageBox::warning(&window, QObject::tr("AIMORA Service"), message);
                });
            }
            if (projectService->state() == aimora::studio::protocol::ServiceProcess::State::Ready) {
                const bool sent = create ? window.createDrawingProject(path, QFileInfo{path}.fileName().chopped(12)) :
                    window.openDrawingProject(path);
                if (!sent) {
                    QMessageBox::warning(&window, QObject::tr("Open Project"),
                                         QObject::tr("Wait for the current project operation to finish."));
                }
            } else {
                pendingProjectPath = path;
                pendingProjectCreation = create;
                projectService->start();
            }
        };
        QObject::connect(openAction, &QAction::triggered, &window, [openOrCreateDrawing]() {
            openOrCreateDrawing(false);
        });
        QAction* newAction = window.commandAction(QStringView{u"file.new"});
        newAction->setEnabled(true);
        newAction->setStatusTip(QObject::tr("Create an empty canonical drawing in a new .aimora.yaml file."));
        QObject::connect(newAction, &QAction::triggered, &window, [openOrCreateDrawing]() {
            openOrCreateDrawing(true);
        });
    }

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
            && window.catalogLibrary()->entryCount() == 16
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
    if(!initialProjectPath.isEmpty()) {
        QTimer::singleShot(0, &window, [&window]() {
            window.commandAction(QStringView{u"file.open"})->trigger();
        });
    }
    return application.exec();
}
