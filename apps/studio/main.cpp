// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/core/application_info.hpp"
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMenuBar>
#include <QSettings>
#include <QStringList>
#include <QTextStream>
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
    parser.addOption(resetWorkspaceOption);
    parser.addOption(windowedOption);
    parser.addOption(themeOption);
    parser.process(application);

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
