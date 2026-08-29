// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/core/application_info.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include <cstdlib>

int main(int argc, char* argv[]) {
    using aimora::studio::core::ApplicationInfo;

    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(ApplicationInfo::productName());
    QCoreApplication::setApplicationVersion(ApplicationInfo::version());
    QCoreApplication::setOrganizationName(QStringLiteral("AIMORA"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Native AIMORAStudio foundation; the graphical shell begins in GUI030."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption architectureOption{
        QStringList{QStringLiteral("architecture")},
        QStringLiteral("Print the frozen native architecture summary."),
    };
    parser.addOption(architectureOption);
    parser.process(application);

    QTextStream output(stdout);
    if(parser.isSet(architectureOption)) {
        output << ApplicationInfo::architectureSummary() << Qt::endl;
        return EXIT_SUCCESS;
    }

    output << ApplicationInfo::productName() << ' ' << ApplicationInfo::version()
           << QStringLiteral(" native foundation") << Qt::endl;
    return EXIT_SUCCESS;
}
