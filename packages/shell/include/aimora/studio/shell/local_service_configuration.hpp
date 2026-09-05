// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include "aimora/studio/protocol/service_process.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <optional>

namespace aimora::studio::shell {

[[nodiscard]] inline std::optional<protocol::ServiceLaunchConfiguration>
localDrawingServiceConfiguration(const QString& program, const QString& project) {
    if(!QDir::isAbsolutePath(program) || !QDir::isAbsolutePath(project) ||
       !QFileInfo{program}.isFile() ||
       !QFileInfo{QDir{project}.filePath(QStringLiteral("Project.toml"))}.isFile() ||
       !QFileInfo{QDir{project}.filePath(QStringLiteral("bin/aimora-service.jl"))}.isFile()) {
        return std::nullopt;
    }
    protocol::ServiceLaunchConfiguration configuration;
    configuration.program = program;
    configuration.programArguments = QStringList{
        QStringLiteral("--startup-file=no"), QStringLiteral("--threads=1"),
        QStringLiteral("--project=%1").arg(project),
        QDir{project}.filePath(QStringLiteral("bin/aimora-service.jl"))};
    configuration.startupTimeoutMs = 120000;
    return configuration;
}

[[nodiscard]] inline std::optional<protocol::ServiceLaunchConfiguration>
configureLocalDrawingService(QWidget& window, QSettings& settings, bool chooseAgain = false) {
    const QString programKey = QStringLiteral("service/juliaExecutable");
    const QString projectKey = QStringLiteral("service/projectDirectory");
    QString program = settings.value(programKey).toString();
    QString project = settings.value(projectKey).toString();
    if(!chooseAgain) {
        if(const auto saved = localDrawingServiceConfiguration(program, project)) {
            return saved;
        }
    }
    QMessageBox::information(&window, QObject::tr("Connect AIMORA Service"),
        QObject::tr("Drawing and saving use the local Julia AIMORAService. "
                    "Choose your Julia executable, then the AIMORAService project folder. "
                    "Only choose a local installation you trust."));
    program = QFileDialog::getOpenFileName(&window, QObject::tr("Choose Julia executable"),
        program, QObject::tr("All files (*)"));
    if(program.isEmpty()) {
        return std::nullopt;
    }
    project = QFileDialog::getExistingDirectory(&window,
        QObject::tr("Choose AIMORAService project folder"), project);
    if(project.isEmpty()) {
        return std::nullopt;
    }
    const auto configuration = localDrawingServiceConfiguration(program, project);
    if(!configuration) {
        QMessageBox::warning(&window, QObject::tr("AIMORA Service setup"),
            QObject::tr("Choose an executable file and a service folder containing "
                        "Project.toml and bin/aimora-service.jl."));
        return std::nullopt;
    }
    settings.setValue(programKey, program);
    settings.setValue(projectKey, project);
    settings.sync();
    return configuration;
}

} // namespace aimora::studio::shell
