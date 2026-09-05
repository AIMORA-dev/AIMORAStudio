// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/local_service_configuration.hpp"
#include "aimora/studio/protocol/service_process.hpp"
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QAction>
#include <QDockWidget>
#include <QApplication>
#include <QLineEdit>
#include <QSettings>
#include <QPixmap>
#include <QFontDatabase>
#include <QFontInfo>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QElapsedTimer>

#include <array>

using aimora::studio::protocol::ServiceClient;
using aimora::studio::protocol::ServiceLaunchConfiguration;
using aimora::studio::protocol::ServiceProcess;
using aimora::studio::protocol::generated::Method;

class JuliaDraftingIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void configuredLocalServiceAuthenticatesAndStops() {
        const QString program = qEnvironmentVariable("AIMORA_NATIVE_JULIA_EXECUTABLE");
        const QString project = qEnvironmentVariable("AIMORA_NATIVE_SERVICE_PROJECT");
        const auto configured = aimora::studio::shell::localDrawingServiceConfiguration(program, project);
        QVERIFY2(configured.has_value(), "A valid Julia executable and AIMORAService project are required.");
        QTemporaryDir root;
        QVERIFY(root.isValid());
        auto configuration = *configured;
        configuration.allowedRoots = {root.path()};
        configuration.maximumAutomaticRestarts = 0;
        aimora::studio::protocol::ServiceProcess service{configuration};
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(
            service.state() == aimora::studio::protocol::ServiceProcess::State::Ready ||
            service.state() == aimora::studio::protocol::ServiceProcess::State::Failed, 120000);
        QCOMPARE(service.state(), aimora::studio::protocol::ServiceProcess::State::Ready);
        QVERIFY(service.client() != nullptr);
        QVERIFY(service.client()->capabilities().contains(QStringLiteral("project.create")));
        QSettings settings{QDir{root.path()}.filePath(QStringLiteral("new-drawing.ini")), QSettings::IniFormat};
        aimora::studio::themes::ThemeSettings themeSettings{settings};
        aimora::studio::themes::ThemeController themeController{*qApp, themeSettings};
        aimora::studio::shell::StudioMainWindow window{themeController, settings};
        window.bindProjectService(service.client());
        const QString path = QDir{root.path()}.filePath(QStringLiteral("blank.aimora.yaml"));
        QSignalSpy createdResponses{service.client(), &ServiceClient::responseReceived};
        QStringList unexpectedDialogs;
        QTimer dialogObserver;
        connect(&dialogObserver, &QTimer::timeout, &window, [&]() {
            if(auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                unexpectedDialogs.append(dialog->windowTitle() + QStringLiteral(": ") + dialog->text());
                dialog->reject();
                service.stop();
            }
        });
        dialogObserver.start(10);
        QElapsedTimer creationTimer;
        creationTimer.start();
        QVERIFY(window.createDrawingProject(path, QStringLiteral("Blank Drawing")));
        QTRY_VERIFY_WITH_TIMEOUT(!window.semanticRevision().isEmpty() || !unexpectedDialogs.isEmpty(), 120000);
        if(!unexpectedDialogs.isEmpty()) {
            QTRY_COMPARE_WITH_TIMEOUT(service.state(), ServiceProcess::State::Stopped, 15000);
        }
        QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
        qInfo() << "New Drawing request completed in milliseconds:" << creationTimer.elapsed();
        QVERIFY(!window.isWindowModified());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(window.commandAction(QStringView{u"draw.line"})->isEnabled());
        QVERIFY(!window.commandAction(QStringView{u"edit.undo"})->isEnabled());
        QVERIFY(!createdResponses.isEmpty());
        const QJsonObject created = qvariant_cast<QJsonObject>(createdResponses.last().at(2));
        QVERIFY(created.value(QStringLiteral("drawing_scene")).toObject().value(QStringLiteral("items")).toArray().isEmpty());
        const QString emptyRevision = window.semanticRevision();
        createdResponses.clear();
        for(const QString& input : {QStringLiteral("l"), QStringLiteral("0,0"), QStringLiteral("10,20")}) {
            QVERIFY(window.drawingWorkspace()->executeCommandText(input));
        }
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != emptyRevision || !unexpectedDialogs.isEmpty(), 120000);
        QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
        QVERIFY(window.isWindowModified());
        QVERIFY(!createdResponses.isEmpty());
        const QJsonObject edited = qvariant_cast<QJsonObject>(createdResponses.last().at(2));
        QCOMPARE(edited.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QJsonObject savedScene = edited.value(QStringLiteral("drawing_scene")).toObject();
        QCOMPARE(savedScene.value(QStringLiteral("items")).toArray().size(), 1);
        for (const QStringList& coordinates : {QStringList{QStringLiteral("2,3"), QStringLiteral("3,5")},
                                               QStringList{QStringLiteral("10,9"), QStringLiteral("11,11")}}) {
            const QString before = window.semanticRevision();
            QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"l"}));
            for (const QString& coordinate : coordinates) {
                QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{coordinate}));
            }
            QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != before || !unexpectedDialogs.isEmpty(), 120000);
            QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
            savedScene = qvariant_cast<QJsonObject>(createdResponses.last().at(2))
                             .value(QStringLiteral("drawing_scene")).toObject();
        }
        QCOMPARE(savedScene.value(QStringLiteral("items")).toArray().size(), 3);
        const QJsonObject originalArrangement = savedScene;
        for (const QString& alias : {QStringLiteral("ax"), QStringLiteral("ay"),
                                     QStringLiteral("dx"), QStringLiteral("dy")}) {
            const bool alignment = alias.startsWith(QLatin1Char('a'));
            const bool horizontal = alias.endsWith(QLatin1Char('x'));
            const QString operation = QStringLiteral("modify.") +
                (alignment ? QStringLiteral("align_anchor_") : QStringLiteral("distribute_anchor_")) +
                (horizontal ? QStringLiteral("x") : QStringLiteral("y"));
            QAction* action = window.commandAction(QStringView{operation});
            QVERIFY(action != nullptr && action->isEnabled());
            QVERIFY(!action->statusTip().isEmpty());
            QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"selectall"}));
            const QString before = window.semanticRevision();
            if (alignment) {
                action->trigger();
                QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"+007.,+.0"}));
            } else {
                QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{alias}));
            }
            QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != before || !unexpectedDialogs.isEmpty(), 120000);
            QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
            const QJsonObject arranged = qvariant_cast<QJsonObject>(createdResponses.last().at(2));
            QCOMPARE(arranged.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            savedScene = arranged.value(QStringLiteral("drawing_scene")).toObject();
            const QJsonArray arrangedItems = savedScene.value(QStringLiteral("items")).toArray();
            QCOMPARE(arrangedItems.size(), 3);
            for (const QJsonValue& originalValue : originalArrangement.value(QStringLiteral("items")).toArray()) {
                const QJsonObject original = originalValue.toObject();
                const QJsonArray originalPoints = original.value(QStringLiteral("points")).toArray();
                const QJsonArray anchor = originalPoints[0].toArray();
                const double originalX = anchor[0].toDouble();
                const double originalY = anchor[1].toDouble();
                const double targetX = !horizontal ? originalX : alignment ? 7.0 : originalX == 2.0 ? 5.0 : originalX;
                const double targetY = horizontal ? originalY : alignment ? 0.0 : originalY == 3.0 ? 4.5 : originalY;
                bool found = false;
                for (const QJsonValue& arrangedValue : arrangedItems) {
                    const QJsonObject item = arrangedValue.toObject();
                    if (item.value(QStringLiteral("owner_id")) != original.value(QStringLiteral("owner_id"))) {
                        continue;
                    }
                    found = true;
                    const QJsonArray actual = item.value(QStringLiteral("points")).toArray();
                    QCOMPARE(actual.size(), originalPoints.size());
                    for (qsizetype index = 0; index < actual.size(); ++index) {
                        const QJsonArray prior = originalPoints[index].toArray();
                        QCOMPARE(actual[index].toArray(),
                                 (QJsonArray{prior[0].toDouble() + targetX - originalX,
                                             prior[1].toDouble() + targetY - originalY}));
                    }
                }
                QVERIFY(found);
            }
            const QString arrangedRevision = window.semanticRevision();
            window.commandAction(QStringView{u"edit.undo"})->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != arrangedRevision || !unexpectedDialogs.isEmpty(), 120000);
            QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
            QCOMPARE(qvariant_cast<QJsonObject>(createdResponses.last().at(2))
                         .value(QStringLiteral("drawing_scene")).toObject(), originalArrangement);
            QVERIFY(window.commandAction(QStringView{u"edit.redo"})->isEnabled());
            const QString undoneRevision = window.semanticRevision();
            window.commandAction(QStringView{u"edit.redo"})->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != undoneRevision || !unexpectedDialogs.isEmpty(), 120000);
            QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
            QCOMPARE(qvariant_cast<QJsonObject>(createdResponses.last().at(2))
                         .value(QStringLiteral("drawing_scene")).toObject(), savedScene);
            if (alias != QStringLiteral("dy")) {
                const QString redoneRevision = window.semanticRevision();
                window.commandAction(QStringView{u"edit.undo"})->trigger();
                QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != redoneRevision || !unexpectedDialogs.isEmpty(), 120000);
                QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
                QCOMPARE(qvariant_cast<QJsonObject>(createdResponses.last().at(2))
                             .value(QStringLiteral("drawing_scene")).toObject(), originalArrangement);
            }
        }
        QVERIFY(window.saveDrawingProject());
        QTRY_VERIFY_WITH_TIMEOUT(!window.isWindowModified() || !unexpectedDialogs.isEmpty(), 120000);
        QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
        service.stop();
        QTRY_COMPARE_WITH_TIMEOUT(service.state(),
            aimora::studio::protocol::ServiceProcess::State::Stopped, 10000);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(service.state() == ServiceProcess::State::Ready ||
            service.state() == ServiceProcess::State::Failed, 120000);
        QCOMPARE(service.state(), ServiceProcess::State::Ready);
        QSignalSpy reopenedResponses{service.client(), &ServiceClient::responseReceived};
        window.bindProjectService(service.client());
        QVERIFY(window.openDrawingProject(path));
        QTRY_VERIFY_WITH_TIMEOUT(!reopenedResponses.isEmpty() || !unexpectedDialogs.isEmpty(), 120000);
        QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QStringLiteral("; "))));
        QVERIFY(!reopenedResponses.isEmpty());
        QVERIFY2(reopenedResponses.last().at(1).toBool(), qPrintable(reopenedResponses.last().at(4).toString()));
        const QJsonObject reopened = qvariant_cast<QJsonObject>(reopenedResponses.last().at(2));
        QCOMPARE(reopened.value(QStringLiteral("drawing_scene")).toObject(), savedScene);
        QVERIFY(!window.isWindowModified());
        QVERIFY(!window.commandAction(QStringView{u"edit.undo"})->isEnabled());
        service.stop();
        QTRY_COMPARE_WITH_TIMEOUT(service.state(), ServiceProcess::State::Stopped, 10000);
    }

    void nativeTextAndPathEditsThroughJulia() {
        const auto configured = aimora::studio::shell::localDrawingServiceConfiguration(
            qEnvironmentVariable("AIMORA_NATIVE_JULIA_EXECUTABLE"),
            qEnvironmentVariable("AIMORA_NATIVE_SERVICE_PROJECT"));
        QVERIFY(configured.has_value());
        QTemporaryDir root;
        QVERIFY(root.isValid());
        auto configuration = *configured;
        configuration.allowedRoots = {root.path()};
        configuration.maximumAutomaticRestarts = 0;
        ServiceProcess service{configuration};
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(service.state() == ServiceProcess::State::Ready ||
                                service.state() == ServiceProcess::State::Failed, 120000);
        QCOMPARE(service.state(), ServiceProcess::State::Ready);
        QSettings settings{QDir{root.path()}.filePath(QStringLiteral("text.ini")), QSettings::IniFormat};
        aimora::studio::themes::ThemeSettings themeSettings{settings};
        aimora::studio::themes::ThemeController themeController{*qApp, themeSettings};
        aimora::studio::shell::StudioMainWindow window{themeController, settings};
        window.bindProjectService(service.client());
        QSignalSpy responses{service.client(), &ServiceClient::responseReceived};
        QStringList errors;
        QStringList answers{QStringLiteral("Feeder A"), QStringLiteral("Feeder B")};
        QTimer dialogs;
        connect(&dialogs, &QTimer::timeout, &window, [&]() {
            if (auto* error = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                errors.append(error->text());
                error->reject();
                service.stop();
            } else if (auto* prompt = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                if (answers.isEmpty()) {
                    errors.append(QStringLiteral("Unexpected text prompt"));
                    prompt->reject();
                } else {
                    prompt->setTextValue(answers.takeFirst());
                    prompt->accept();
                }
            }
        });
        dialogs.start(10);
        const QString path = QDir{root.path()}.filePath(QStringLiteral("text.aimora.yaml"));
        QVERIFY(window.createDrawingProject(path, QStringLiteral("Text Editing")));
        QTRY_VERIFY_WITH_TIMEOUT(!window.semanticRevision().isEmpty() || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        const QString emptyRevision = window.semanticRevision();
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"t"}));
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"1,2"}));
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != emptyRevision || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        const QJsonObject originalScene = qvariant_cast<QJsonObject>(responses.last().at(2))
            .value(QStringLiteral("drawing_scene")).toObject();
        QCOMPARE(originalScene.value(QStringLiteral("items")).toArray().size(), 1);
        QCOMPARE(answers.size(), 1);
        QVERIFY(window.commandAction(QStringView{u"modify.text"})->isEnabled());
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"selectall"}));
        const QString beforeEdit = window.semanticRevision();
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"ed"}));
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeEdit || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        QVERIFY(answers.isEmpty());
        const QJsonObject editedScene = qvariant_cast<QJsonObject>(responses.last().at(2))
            .value(QStringLiteral("drawing_scene")).toObject();
        const QJsonObject originalItem = originalScene.value(QStringLiteral("items")).toArray()[0].toObject();
        const QJsonObject editedItem = editedScene.value(QStringLiteral("items")).toArray()[0].toObject();
        QCOMPARE(editedItem.value(QStringLiteral("text")).toString(), QStringLiteral("Feeder B"));
        QCOMPARE(editedItem.value(QStringLiteral("owner_id")), originalItem.value(QStringLiteral("owner_id")));
        QCOMPARE(editedItem.value(QStringLiteral("points")), originalItem.value(QStringLiteral("points")));
        const QString editedRevision = window.semanticRevision();
        window.commandAction(QStringView{u"edit.undo"})->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != editedRevision || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        QCOMPARE(qvariant_cast<QJsonObject>(responses.last().at(2))
                     .value(QStringLiteral("drawing_scene")).toObject(), originalScene);
        const QString undoneRevision = window.semanticRevision();
        window.commandAction(QStringView{u"edit.redo"})->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != undoneRevision || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        QCOMPARE(qvariant_cast<QJsonObject>(responses.last().at(2))
                     .value(QStringLiteral("drawing_scene")).toObject(), editedScene);
        QVERIFY(window.saveDrawingProject());
        QTRY_VERIFY_WITH_TIMEOUT(!window.isWindowModified() || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        const QString beforeNewDrawing = window.semanticRevision();
        const QString pathsFile = QDir{root.path()}.filePath(QStringLiteral("paths.aimora.yaml"));
        QVERIFY(window.createDrawingProject(pathsFile, QStringLiteral("Explode Paths")));
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeNewDrawing || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        const QString beforeRectangle = window.semanticRevision();
        for (const QString& input : {QStringLiteral("rec"), QStringLiteral("0,0"), QStringLiteral("10,5")}) {
            QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{input}));
        }
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeRectangle || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        const QJsonObject rectangleScene = qvariant_cast<QJsonObject>(responses.last().at(2))
            .value(QStringLiteral("drawing_scene")).toObject();
        QCOMPARE(rectangleScene.value(QStringLiteral("items")).toArray().size(), 1);
        QVERIFY(window.commandAction(QStringView{u"modify.explode_paths"})->isEnabled());
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"selectall"}));
        const QString beforeExplosion = window.semanticRevision();
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"x"}));
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeExplosion || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        const QJsonObject explodedScene = qvariant_cast<QJsonObject>(responses.last().at(2))
            .value(QStringLiteral("drawing_scene")).toObject();
        const QJsonArray lines = explodedScene.value(QStringLiteral("items")).toArray();
        QCOMPARE(lines.size(), 4);
        QJsonArray expectedEdges{
            QJsonArray{QJsonArray{0, 0}, QJsonArray{10, 0}},
            QJsonArray{QJsonArray{10, 0}, QJsonArray{10, 5}},
            QJsonArray{QJsonArray{10, 5}, QJsonArray{0, 5}},
            QJsonArray{QJsonArray{0, 5}, QJsonArray{0, 0}}};
        for (const QJsonValue& value : lines) {
            const QJsonObject item = value.toObject();
            const QJsonValue points = item.value(QStringLiteral("points"));
            QVERIFY(expectedEdges.contains(points));
            for (qsizetype index = 0; index < expectedEdges.size(); ++index) {
                if (expectedEdges[index] == points) {
                    expectedEdges.removeAt(index);
                    break;
                }
            }
        }
        QVERIFY(expectedEdges.isEmpty());
        const QString explodedRevision = window.semanticRevision();
        window.commandAction(QStringView{u"edit.undo"})->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != explodedRevision || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        QCOMPARE(qvariant_cast<QJsonObject>(responses.last().at(2))
                     .value(QStringLiteral("drawing_scene")).toObject(), rectangleScene);
        const QString restoredRevision = window.semanticRevision();
        window.commandAction(QStringView{u"edit.redo"})->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != restoredRevision || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        QCOMPARE(qvariant_cast<QJsonObject>(responses.last().at(2))
                     .value(QStringLiteral("drawing_scene")).toObject(), explodedScene);
        QVERIFY(window.commandAction(QStringView{u"modify.join_lines"})->isEnabled());
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"selectall"}));
        const QString beforeJoin = window.semanticRevision();
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{u"j"}));
        QVERIFY(window.drawingWorkspace()->executeCommandText(QStringView{}));
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeJoin || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        const QJsonObject joinedScene = qvariant_cast<QJsonObject>(responses.last().at(2))
            .value(QStringLiteral("drawing_scene")).toObject();
        const QJsonArray joinedItems = joinedScene.value(QStringLiteral("items")).toArray();
        QCOMPARE(joinedItems.size(), 1);
        const QJsonArray joinedPoints = joinedItems[0].toObject().value(QStringLiteral("points")).toArray();
        QCOMPARE(joinedPoints, (QJsonArray{QJsonArray{0, 0}, QJsonArray{0, 5},
                                         QJsonArray{10, 5}, QJsonArray{10, 0}, QJsonArray{0, 0}}));
        const QString joinedRevision = window.semanticRevision();
        window.commandAction(QStringView{u"edit.undo"})->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != joinedRevision || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        QCOMPARE(qvariant_cast<QJsonObject>(responses.last().at(2))
                     .value(QStringLiteral("drawing_scene")).toObject(), explodedScene);
        const QString unjoinedRevision = window.semanticRevision();
        window.commandAction(QStringView{u"edit.redo"})->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != unjoinedRevision || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        QCOMPARE(qvariant_cast<QJsonObject>(responses.last().at(2))
                     .value(QStringLiteral("drawing_scene")).toObject(), joinedScene);
        QVERIFY(window.saveDrawingProject());
        QTRY_VERIFY_WITH_TIMEOUT(!window.isWindowModified() || !errors.isEmpty(), 120000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
        service.stop();
        QTRY_COMPARE_WITH_TIMEOUT(service.state(), ServiceProcess::State::Stopped, 10000);
    }

    void initTestCase() {
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QVERIFY(QFontDatabase::families().contains(QStringLiteral("Segoe UI")));
            QApplication::setFont(QFont{QStringLiteral("Segoe UI"), 10});
            QCOMPARE(QFontInfo{QApplication::font()}.family(), QStringLiteral("Segoe UI"));
        }
#endif
    }

    void canonicalDrawingCommitsSavesAndReopensThroughNativeTransport() {
        const QString julia = qEnvironmentVariable("AIMORA_NATIVE_JULIA_EXECUTABLE");
        const QString serviceProject = qEnvironmentVariable("AIMORA_NATIVE_SERVICE_PROJECT");
        const QString fixtureScript = qEnvironmentVariable("AIMORA_NATIVE_DRAWING_FIXTURE");
        QVERIFY2(!julia.isEmpty() && QFileInfo::exists(serviceProject)
                     && QFileInfo::exists(fixtureScript),
                 "Real Julia integration requires explicit executable, service project, and fixture paths.");
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString drawingPath = QDir{root.path()}.filePath(QStringLiteral("native.aimora.yaml"));
        QDir projectDirectory{QFileInfo{fixtureScript}.absolutePath()};
        QVERIFY(projectDirectory.cdUp());

        QProcess fixture;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("JULIA_NUM_THREADS"), QStringLiteral("1"));
        environment.insert(QStringLiteral("OPENBLAS_NUM_THREADS"), QStringLiteral("1"));
        fixture.setProcessEnvironment(environment);
        QSignalSpy fixtureFinished{&fixture, qOverload<int, QProcess::ExitStatus>(&QProcess::finished)};
        fixture.start(julia, QStringList{
            QStringLiteral("--startup-file=no"),
            QStringLiteral("--project=") + projectDirectory.absolutePath(), fixtureScript, drawingPath});
        QTRY_COMPARE_WITH_TIMEOUT(fixtureFinished.count(), 1, 120000);
        const QByteArray fixtureErrors = fixture.readAllStandardError();
        QVERIFY2(fixture.exitStatus() == QProcess::NormalExit && fixture.exitCode() == 0,
                 fixtureErrors.constData());
        QFile drawing{drawingPath};
        QVERIFY(drawing.open(QIODevice::ReadOnly));
        const QByteArray originalBytes = drawing.readAll();
        drawing.close();
        QVERIFY(!originalBytes.isEmpty());

        ServiceLaunchConfiguration configuration;
        configuration.program = julia;
        configuration.programArguments = QStringList{
            QStringLiteral("--startup-file=no"), QStringLiteral("--threads=1"),
            QStringLiteral("--project=") + serviceProject,
            QDir{serviceProject}.filePath(QStringLiteral("bin/aimora-service.jl"))};
        configuration.allowedRoots = QStringList{root.path()};
        configuration.startupTimeoutMs = 120000;
        configuration.shutdownTimeoutMs = 10000;
        configuration.maximumAutomaticRestarts = 0;
        ServiceProcess process{configuration};
        process.start();
        QTRY_VERIFY_WITH_TIMEOUT(process.state() == ServiceProcess::State::Ready
                                   || process.state() == ServiceProcess::State::Failed, 120000);
        QVERIFY2(process.state() == ServiceProcess::State::Ready,
                 qPrintable(process.failureCode() + QStringLiteral(": ") + process.failureMessage()));
        ServiceClient* client = process.client();
        QVERIFY(client != nullptr);
        QVERIFY(client->capabilities().contains(QStringLiteral("project.save")));
        QSignalSpy responses{client, &ServiceClient::responseReceived};
        const QString request = client->sendRequest(Method::ProjectOpen, QJsonObject{
            {QStringLiteral("path"), drawingPath},
            {QStringLiteral("mode"), QStringLiteral("drafting")}});
        QVERIFY(!request.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 120000);
        const QList<QVariant> response = responses.at(0);
        QCOMPARE(response.at(0).toString(), request);
        QVERIFY2(response.at(1).toBool(), qPrintable(response.at(3).toString()
            + QStringLiteral(": ") + response.at(4).toString()));
        const QJsonObject result = qvariant_cast<QJsonObject>(response.at(2));
        QVERIFY(!result.isEmpty());
        const QString projectId = result.value(QStringLiteral("project_id")).toString();
        const QString baseRevision = result.value(QStringLiteral("revision")).toString();
        QVERIFY(!projectId.isEmpty());
        QVERIFY(!baseRevision.isEmpty());
        QVERIFY(result.value(QStringLiteral("can_save")).toBool());
        QVERIFY(!result.value(QStringLiteral("modified")).toBool());
        QVERIFY(drawing.open(QIODevice::ReadOnly));
        QCOMPARE(drawing.readAll(), originalBytes);
        drawing.close();

        const auto semanticMethod = aimora::studio::protocol::generated::parseMethod(QStringView{u"semantic.commit"});
        QVERIFY(semanticMethod.has_value());
        responses.clear();
        const QString editRequest = client->sendRequest(*semanticMethod, QJsonObject{
            {QStringLiteral("project_id"), projectId},
            {QStringLiteral("base_revision"), baseRevision},
            {QStringLiteral("transaction_id"), QStringLiteral("native-drawing-line")},
            {QStringLiteral("operation"), QStringLiteral("draw.line")},
            {QStringLiteral("semantic_ids"), QJsonArray{}},
            {QStringLiteral("points"), QJsonArray{QJsonValue{QJsonArray{10, 20}}, QJsonValue{QJsonArray{30, 40}}}},
            {QStringLiteral("attributes"), QJsonObject{}}
        });
        QVERIFY(!editRequest.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 120000);
        const QList<QVariant> editResponse = responses.at(0);
        QCOMPARE(editResponse.at(0).toString(), editRequest);
        QVERIFY2(editResponse.at(1).toBool(), qPrintable(editResponse.at(3).toString()
            + QStringLiteral(": ") + editResponse.at(4).toString()));
        const QJsonObject editResult = qvariant_cast<QJsonObject>(editResponse.at(2));
        QCOMPARE(editResult.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QString committedRevision = editResult.value(QStringLiteral("revision")).toString();
        QVERIFY(!committedRevision.isEmpty());
        QVERIFY(committedRevision != baseRevision);
        QJsonObject committedScene = editResult.value(QStringLiteral("drawing_scene")).toObject();
        QCOMPARE(committedScene.value(QStringLiteral("items")).toArray().size(), 1);

        const QJsonArray lineOwners = editResult.value(QStringLiteral("changed_owner_ids")).toArray();
        QCOMPARE(lineOwners.size(), 1);
        const QString lineOwner = lineOwners.at(0).toString();
        QVERIFY(!lineOwner.isEmpty());
        struct DraftingStep final {
            QString operation;
            QJsonArray points;
            QJsonObject attributes;
            qsizetype expectedItems;
        };
        const std::array<DraftingStep, 8> draftingSteps{{
            {QStringLiteral("draw.circle"),
             QJsonArray{QJsonValue{QJsonArray{100, 100}}, QJsonValue{QJsonArray{110, 100}}}, {}, 2},
            {QStringLiteral("draw.rectangle"),
             QJsonArray{QJsonValue{QJsonArray{0, 0}}, QJsonValue{QJsonArray{20, 10}}}, {}, 3},
            {QStringLiteral("draw.polyline"),
             QJsonArray{QJsonValue{QJsonArray{0, 0}}, QJsonValue{QJsonArray{5, 0}}, QJsonValue{QJsonArray{5, 5}}}, {}, 4},
            {QStringLiteral("draw.text"), QJsonArray{QJsonValue{QJsonArray{3, 4}}},
             QJsonObject{{QStringLiteral("text"), QStringLiteral("Main bus")}}, 5},
            {QStringLiteral("modify.copy"),
             QJsonArray{QJsonValue{QJsonArray{0, 0}}, QJsonValue{QJsonArray{10, 0}}}, {}, 6},
            {QStringLiteral("modify.move"),
             QJsonArray{QJsonValue{QJsonArray{0, 0}}, QJsonValue{QJsonArray{10, 10}}}, {}, 6},
            {QStringLiteral("modify.scale"), QJsonArray{QJsonValue{QJsonArray{0, 0}}},
             QJsonObject{{QStringLiteral("factor"), QStringLiteral("2")}}, 6},
            {QStringLiteral("modify.erase"), {}, {}, 5},
        }};
        QString copiedOwner;
        qsizetype stepIndex = 0;
        for(const DraftingStep& step : draftingSteps) {
            QJsonArray targets;
            if(step.operation == QStringLiteral("modify.copy")) {
                targets.append(lineOwner);
            } else if(step.operation.startsWith(QStringLiteral("modify."))) {
                QVERIFY(!copiedOwner.isEmpty());
                targets.append(copiedOwner);
            }
            responses.clear();
            const QString stepRequest = client->sendRequest(*semanticMethod, QJsonObject{
                {QStringLiteral("project_id"), projectId},
                {QStringLiteral("base_revision"), committedRevision},
                {QStringLiteral("transaction_id"), QStringLiteral("native-drawing-step-%1").arg(++stepIndex)},
                {QStringLiteral("operation"), step.operation},
                {QStringLiteral("semantic_ids"), targets},
                {QStringLiteral("points"), step.points},
                {QStringLiteral("attributes"), step.attributes}});
            QVERIFY(!stepRequest.isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 120000);
            const QList<QVariant> stepResponse = responses.at(0);
            QCOMPARE(stepResponse.at(0).toString(), stepRequest);
            QVERIFY2(stepResponse.at(1).toBool(), qPrintable(step.operation + QStringLiteral(": ")
                + stepResponse.at(3).toString() + QStringLiteral(": ") + stepResponse.at(4).toString()));
            const QJsonObject stepResult = qvariant_cast<QJsonObject>(stepResponse.at(2));
            QCOMPARE(stepResult.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            const QString nextRevision = stepResult.value(QStringLiteral("revision")).toString();
            QVERIFY(!nextRevision.isEmpty());
            QVERIFY(nextRevision != committedRevision);
            committedRevision = nextRevision;
            committedScene = stepResult.value(QStringLiteral("drawing_scene")).toObject();
            QCOMPARE(committedScene.value(QStringLiteral("items")).toArray().size(), step.expectedItems);
            if(step.operation == QStringLiteral("modify.copy")) {
                const QJsonArray copiedOwners = stepResult.value(QStringLiteral("changed_owner_ids")).toArray();
                QCOMPARE(copiedOwners.size(), 1);
                copiedOwner = copiedOwners.at(0).toString();
                QVERIFY(!copiedOwner.isEmpty());
                QVERIFY(copiedOwner != lineOwner);
            }
        }

        responses.clear();
        const QString staleSave = client->sendRequest(Method::ProjectSave, QJsonObject{
            {QStringLiteral("project_id"), projectId}, {QStringLiteral("base_revision"), baseRevision}});
        QVERIFY(!staleSave.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 120000);
        QCOMPARE(responses.at(0).at(0).toString(), staleSave);
        QVERIFY(!responses.at(0).at(1).toBool());
        QCOMPARE(responses.at(0).at(3).toString(), QStringLiteral("REVISION_CONFLICT"));
        QVERIFY(drawing.open(QIODevice::ReadOnly));
        QCOMPARE(drawing.readAll(), originalBytes);
        drawing.close();

        responses.clear();
        const QString saveRequest = client->sendRequest(Method::ProjectSave, QJsonObject{
            {QStringLiteral("project_id"), projectId}, {QStringLiteral("base_revision"), committedRevision}});
        QVERIFY(!saveRequest.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 120000);
        const QList<QVariant> saveResponse = responses.at(0);
        QCOMPARE(saveResponse.at(0).toString(), saveRequest);
        QVERIFY2(saveResponse.at(1).toBool(), qPrintable(saveResponse.at(3).toString()
            + QStringLiteral(": ") + saveResponse.at(4).toString()));
        const QJsonObject saveResult = qvariant_cast<QJsonObject>(saveResponse.at(2));
        QVERIFY(saveResult.value(QStringLiteral("saved")).toBool());
        QVERIFY(!saveResult.value(QStringLiteral("modified")).toBool());
        QCOMPARE(saveResult.value(QStringLiteral("revision")).toString(), committedRevision);
        QVERIFY(drawing.open(QIODevice::ReadOnly));
        QByteArray savedBytes = drawing.readAll();
        drawing.close();
        QVERIFY(!savedBytes.isEmpty());
        QVERIFY(savedBytes != originalBytes);
        {
            QSettings settings{QDir{root.path()}.filePath(QStringLiteral("studio.ini")), QSettings::IniFormat};
            aimora::studio::themes::ThemeSettings themeSettings{settings};
            aimora::studio::themes::ThemeController themeController{*qApp, themeSettings};
            aimora::studio::shell::StudioMainWindow window{themeController, settings};
            window.bindProjectService(client);
            QVERIFY(window.openDrawingProject(drawingPath));
            QAction* saveAction = window.commandAction(QStringView{u"file.save"});
            QVERIFY(saveAction != nullptr);
            QTRY_VERIFY_WITH_TIMEOUT(saveAction->isEnabled(), 120000);
            QVERIFY(!window.isWindowModified());
            QLineEdit* commandLine = window.findChild<QLineEdit*>(QStringLiteral("aimora.command-line"));
            QVERIFY(commandLine != nullptr);
            for(QWidget* ancestor = commandLine->parentWidget(); ancestor != nullptr; ancestor = ancestor->parentWidget()) {
                if(auto* dock = qobject_cast<QDockWidget*>(ancestor)) {
                    dock->show();
                    break;
                }
            }
            window.show();
            window.activateWindow();
            QTRY_VERIFY(window.isActiveWindow());
            commandLine->setFocus();
            responses.clear();
            for(const QString& input : {QStringLiteral("l"), QStringLiteral("0.1000000000000000000000000000000001,50"), QStringLiteral("@0.2,20")}) {
                QTest::keyClicks(commandLine, input);
                QTest::keyClick(commandLine, Qt::Key_Return);
            }
            QTest::keyClick(commandLine, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(window.isWindowModified(), 120000);
            QJsonObject guiEditResult;
            for(const QList<QVariant>& received : responses) {
                const QJsonObject candidate = qvariant_cast<QJsonObject>(received.at(2));
                if(received.at(1).toBool()
                    && candidate.value(QStringLiteral("status")).toString() == QStringLiteral("accepted")) {
                    guiEditResult = candidate;
                }
            }
            QVERIFY(!guiEditResult.isEmpty());
            committedScene = guiEditResult.value(QStringLiteral("drawing_scene")).toObject();
            QCOMPARE(committedScene.value(QStringLiteral("items")).toArray().size(), 6);
            const QString beforeMove = window.semanticRevision();
            responses.clear();
            for(const QString& input : {QStringLiteral("selectall"), QStringLiteral("m"),
                                        QStringLiteral("0,0"), QStringLiteral("@10,0")}) {
                QTest::keyClicks(commandLine, input);
                QTest::keyClick(commandLine, Qt::Key_Return);
            }
            QTest::keyClick(commandLine, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeMove, 120000);
            QJsonObject guiMoveResult;
            for(const QList<QVariant>& received : responses) {
                const QJsonObject candidate = qvariant_cast<QJsonObject>(received.at(2));
                if(received.at(1).toBool()
                    && candidate.value(QStringLiteral("status")).toString() == QStringLiteral("accepted")) {
                    guiMoveResult = candidate;
                }
            }
            QVERIFY(!guiMoveResult.isEmpty());
            QCOMPARE(guiMoveResult.value(QStringLiteral("changed_owner_ids")).toArray().size(), 6);
            committedScene = guiMoveResult.value(QStringLiteral("drawing_scene")).toObject();
            QCOMPARE(committedScene.value(QStringLiteral("items")).toArray().size(), 6);

            QTest::keyClick(commandLine, Qt::Key_Escape);
            QAction* arcAction = window.commandAction(QStringView{u"draw.arc"});
            QVERIFY(arcAction != nullptr);
            QVERIFY(arcAction->isEnabled());
            QVERIFY(!arcAction->statusTip().isEmpty());
            arcAction->trigger();
            commandLine->setFocus();
            const QString beforeArc = window.semanticRevision();
            responses.clear();
            for(const QString& input : {QStringLiteral("50,0"), QStringLiteral("51,0"),
                                        QStringLiteral("52,0")}) {
                QTest::keyClicks(commandLine, input);
                QTest::keyClick(commandLine, Qt::Key_Return);
            }
            QTest::keyClick(commandLine, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty(), 120000);
            QCOMPARE(window.semanticRevision(), beforeArc);
            QVERIFY(!responses.last().at(1).toBool());
            QCOMPARE(responses.last().at(3).toString(), QStringLiteral("SEMANTIC_EDIT_REJECTED"));

            responses.clear();
            for(const QString& input : {QStringLiteral("u"), QStringLiteral("@0,1")}) {
                QTest::keyClicks(commandLine, input);
                QTest::keyClick(commandLine, Qt::Key_Return);
            }
            QTest::keyClick(commandLine, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeArc, 120000);
            QVERIFY(!responses.isEmpty());
            const QJsonObject acceptedArc = qvariant_cast<QJsonObject>(responses.last().at(2));
            QCOMPARE(acceptedArc.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            const QJsonArray arcOwners = acceptedArc.value(QStringLiteral("changed_owner_ids")).toArray();
            QCOMPARE(arcOwners.size(), 1);
            committedScene = acceptedArc.value(QStringLiteral("drawing_scene")).toObject();
            const QJsonArray arcSceneItems = committedScene.value(QStringLiteral("items")).toArray();
            QCOMPARE(arcSceneItems.size(), 7);
            QJsonObject arcItem;
            for(const QJsonValue& item : arcSceneItems) {
                const QJsonObject candidate = item.toObject();
                if(candidate.value(QStringLiteral("owner_id")) == arcOwners.first()) {
                    arcItem = candidate;
                }
            }
            QVERIFY(!arcItem.isEmpty());
            QCOMPARE(arcItem.value(QStringLiteral("kind")).toString(), QStringLiteral("polyline"));
            const QJsonArray arcPoints = arcItem.value(QStringLiteral("points")).toArray();
            QVERIFY(arcPoints.size() > 3);
            QCOMPARE(arcPoints.first().toArray(), (QJsonArray{50, 0}));
            QCOMPARE(arcPoints.last().toArray(), (QJsonArray{51, 1}));
            QVERIFY(arcPoints.contains(QJsonValue{QJsonArray{51, 0}}));
            QAction* ellipseAction = window.commandAction(QStringView{u"draw.ellipse"});
            QVERIFY(ellipseAction != nullptr);
            QVERIFY(ellipseAction->isEnabled());
            QVERIFY(!ellipseAction->statusTip().isEmpty());
            ellipseAction->trigger();
            commandLine->setFocus();
            const QString beforeEllipse = window.semanticRevision();
            responses.clear();
            for(const QString& input : {QStringLiteral("80,20"), QStringLiteral("@8,4")}) {
                QTest::keyClicks(commandLine, input);
                QTest::keyClick(commandLine, Qt::Key_Return);
            }
            QTest::keyClick(commandLine, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeEllipse, 120000);
            QVERIFY(!responses.isEmpty());
            QVERIFY2(responses.last().at(1).toBool(), qPrintable(responses.last().at(4).toString()));
            const QJsonObject acceptedEllipse = qvariant_cast<QJsonObject>(responses.last().at(2));
            QCOMPARE(acceptedEllipse.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            const QJsonArray ellipseOwners = acceptedEllipse.value(QStringLiteral("changed_owner_ids")).toArray();
            QCOMPARE(ellipseOwners.size(), 1);
            committedScene = acceptedEllipse.value(QStringLiteral("drawing_scene")).toObject();
            const QJsonArray ellipseSceneItems = committedScene.value(QStringLiteral("items")).toArray();
            QCOMPARE(ellipseSceneItems.size(), 8);
            QJsonObject ellipseItem;
            for(const QJsonValue& item : ellipseSceneItems) {
                const QJsonObject candidate = item.toObject();
                if(candidate.value(QStringLiteral("owner_id")) == ellipseOwners.first()) {
                    ellipseItem = candidate;
                }
            }
            QVERIFY(!ellipseItem.isEmpty());
            QCOMPARE(ellipseItem.value(QStringLiteral("kind")).toString(), QStringLiteral("polyline"));
            const QJsonArray ellipsePoints = ellipseItem.value(QStringLiteral("points")).toArray();
            QCOMPARE(ellipsePoints.size(), 129);
            QCOMPARE(ellipsePoints.first().toArray(), (QJsonArray{88, 22}));
            QCOMPARE(ellipsePoints.first(), ellipsePoints.last());
            for(const QJsonValue& point : ellipsePoints) {
                const QJsonArray coordinates = point.toArray();
                QCOMPARE(coordinates.size(), 2);
                const double x = (coordinates.at(0).toDouble() - 84) / 4;
                const double y = (coordinates.at(1).toDouble() - 22) / 2;
                QVERIFY(qAbs(x * x + y * y - 1) < 1e-12);
            }
            QAction* rotationAction = window.commandAction(QStringView{u"modify.rotate_quarter"});
            QVERIFY(rotationAction != nullptr);
            QVERIFY(rotationAction->isEnabled());
            const QJsonObject beforeRotationScene = committedScene;
            for(int turn = 0; turn < 4; ++turn) {
                const QString beforeRotationRevision = window.semanticRevision();
                responses.clear();
                QTest::keyClicks(commandLine, "selectall");
                QTest::keyClick(commandLine, Qt::Key_Return);
                rotationAction->trigger();
                commandLine->setFocus();
                QTest::keyClicks(commandLine, "1.25,-2.5");
                QTest::keyClick(commandLine, Qt::Key_Return);
                QTest::keyClick(commandLine, Qt::Key_Return);
                QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeRotationRevision, 120000);
                QVERIFY(!responses.isEmpty());
                QVERIFY2(responses.last().at(1).toBool(), qPrintable(responses.last().at(4).toString()));
                const QJsonObject rotated = qvariant_cast<QJsonObject>(responses.last().at(2));
                QCOMPARE(rotated.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
                QCOMPARE(rotated.value(QStringLiteral("changed_owner_ids")).toArray().size(), 8);
                committedScene = rotated.value(QStringLiteral("drawing_scene")).toObject();
                QCOMPARE(committedScene.value(QStringLiteral("items")).toArray().size(), 8);
                if(turn < 3) {
                    QVERIFY(committedScene != beforeRotationScene);
                } else {
                    QCOMPARE(committedScene, beforeRotationScene);
                }
            }
            QAction* undoAction = window.commandAction(QStringView{u"edit.undo"});
            QAction* redoAction = window.commandAction(QStringView{u"edit.redo"});
            QVERIFY(undoAction != nullptr);
            QVERIFY(redoAction != nullptr);
            QVERIFY(undoAction->isEnabled());
            QVERIFY(!redoAction->isEnabled());
            const QJsonObject beforeHistoryScene = committedScene;
            const QString beforeUndoRevision = window.semanticRevision();
            responses.clear();
            QTest::keySequence(&window, QKeySequence{QKeySequence::Undo});
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeUndoRevision, 120000);
            QVERIFY(!responses.isEmpty());
            QVERIFY2(responses.last().at(1).toBool(), qPrintable(responses.last().at(4).toString()));
            const QJsonObject undoneDrawing = qvariant_cast<QJsonObject>(responses.last().at(2));
            QCOMPARE(undoneDrawing.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            QVERIFY(undoneDrawing.value(QStringLiteral("drawing_scene")).toObject() != beforeHistoryScene);
            QCOMPARE(undoneDrawing.value(QStringLiteral("changed_owner_ids")).toArray().size(), 8);
            QVERIFY(redoAction->isEnabled());
            const QString beforeRedoRevision = window.semanticRevision();
            responses.clear();
            redoAction->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeRedoRevision, 120000);
            QVERIFY(!responses.isEmpty());
            QVERIFY2(responses.last().at(1).toBool(), qPrintable(responses.last().at(4).toString()));
            const QJsonObject redoneDrawing = qvariant_cast<QJsonObject>(responses.last().at(2));
            QCOMPARE(redoneDrawing.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            committedScene = redoneDrawing.value(QStringLiteral("drawing_scene")).toObject();
            QCOMPARE(committedScene, beforeHistoryScene);
            QVERIFY(undoAction->isEnabled());
            QVERIFY(!redoAction->isEnabled());
            const QString beforeBranchUndo = window.semanticRevision();
            responses.clear();
            undoAction->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeBranchUndo, 120000);
            QVERIFY(redoAction->isEnabled());
            const QString beforeBranchEdit = window.semanticRevision();
            responses.clear();
            commandLine->setFocus();
            for(const QString& input : {QStringLiteral("selectall"), QStringLiteral("r90"),
                                        QStringLiteral("1.25,-2.5")}) {
                QTest::keyClicks(commandLine, input);
                QTest::keyClick(commandLine, Qt::Key_Return);
            }
            QTest::keyClick(commandLine, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeBranchEdit, 120000);
            QVERIFY(!responses.isEmpty());
            QVERIFY2(responses.last().at(1).toBool(), qPrintable(responses.last().at(4).toString()));
            const QJsonObject branchedDrawing = qvariant_cast<QJsonObject>(responses.last().at(2));
            QCOMPARE(branchedDrawing.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            committedScene = branchedDrawing.value(QStringLiteral("drawing_scene")).toObject();
            QCOMPARE(committedScene, beforeHistoryScene);
            QVERIFY(undoAction->isEnabled());
            QVERIFY(!redoAction->isEnabled());
            const QJsonObject beforeMirrorScene = committedScene;
            for(const QString& alias : {QStringLiteral("miv"), QStringLiteral("mih")}) {
                const QString commandId = alias == QStringLiteral("miv") ?
                    QStringLiteral("modify.mirror_vertical") : QStringLiteral("modify.mirror_horizontal");
                QAction* mirrorAction = window.commandAction(QStringView{commandId});
                QVERIFY(mirrorAction != nullptr);
                QVERIFY(mirrorAction->isEnabled());
                for(int reflection = 0; reflection < 2; ++reflection) {
                    const QString beforeMirrorRevision = window.semanticRevision();
                    responses.clear();
                    for(const QString& input : {QStringLiteral("selectall"), alias, QStringLiteral("0,0")}) {
                        QTest::keyClicks(commandLine, input);
                        QTest::keyClick(commandLine, Qt::Key_Return);
                    }
                    QTest::keyClick(commandLine, Qt::Key_Return);
                    QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != beforeMirrorRevision, 120000);
                    QVERIFY(!responses.isEmpty());
                    QVERIFY2(responses.last().at(1).toBool(), qPrintable(responses.last().at(4).toString()));
                    const QJsonObject mirrored = qvariant_cast<QJsonObject>(responses.last().at(2));
                    QCOMPARE(mirrored.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
                    QCOMPARE(mirrored.value(QStringLiteral("changed_owner_ids")).toArray().size(), 8);
                    committedScene = mirrored.value(QStringLiteral("drawing_scene")).toObject();
                    QCOMPARE(committedScene.value(QStringLiteral("items")).toArray().size(), 8);
                    if(reflection == 0) {
                        QVERIFY(committedScene != beforeMirrorScene);
                    } else {
                        QCOMPARE(committedScene, beforeMirrorScene);
                    }
                }
            }
            QTest::keyClick(commandLine, Qt::Key_S, Qt::ControlModifier);
            QTRY_VERIFY_WITH_TIMEOUT(!window.isWindowModified(), 120000);
            const QString savedContentRevision = window.semanticRevision();
            responses.clear();
            for(const QString& input : {QStringLiteral("selectall"), QStringLiteral("r90"),
                                        QStringLiteral("1.25,-2.5")}) {
                QTest::keyClicks(commandLine, input);
                QTest::keyClick(commandLine, Qt::Key_Return);
            }
            QTest::keyClick(commandLine, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != savedContentRevision, 120000);
            QVERIFY(window.isWindowModified());
            const QString modifiedContentRevision = window.semanticRevision();
            responses.clear();
            undoAction->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(window.semanticRevision() != modifiedContentRevision, 120000);
            QVERIFY(!window.isWindowModified());
            QVERIFY(redoAction->isEnabled());
            QVERIFY(!responses.isEmpty());
            const QJsonObject restoredSavedDrawing = qvariant_cast<QJsonObject>(responses.last().at(2));
            QCOMPARE(restoredSavedDrawing.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            QCOMPARE(restoredSavedDrawing.value(QStringLiteral("drawing_scene")).toObject(), committedScene);
            const QString artifactDirectory = qEnvironmentVariable("AIMORA_NATIVE_GUI_ARTIFACT_DIR");
            if(!artifactDirectory.isEmpty()) {
                QTest::keyClicks(commandLine, "ze");
                QTest::keyClick(commandLine, Qt::Key_Return);
                QVERIFY(commandLine->text().isEmpty());
                QVERIFY(QDir{}.mkpath(artifactDirectory));
                QVERIFY(window.grab().save(QDir{artifactDirectory}.filePath(QStringLiteral("drafting-window.png"))));
            }
            QVERIFY(drawing.open(QIODevice::ReadOnly));
            const QByteArray guiSavedBytes = drawing.readAll();
            drawing.close();
            QVERIFY(!guiSavedBytes.isEmpty());
            QVERIFY(guiSavedBytes != savedBytes);
            savedBytes = guiSavedBytes;
        }
        process.stop();
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Stopped, 15000);

        process.start();
        QTRY_VERIFY_WITH_TIMEOUT(process.state() == ServiceProcess::State::Ready
                                   || process.state() == ServiceProcess::State::Failed, 120000);
        QVERIFY2(process.state() == ServiceProcess::State::Ready, qPrintable(process.failureMessage()));
        QVERIFY(process.client() != nullptr);
        QSignalSpy reopenedResponses{process.client(), &ServiceClient::responseReceived};
        const QString reopenRequest = process.client()->sendRequest(Method::ProjectOpen, QJsonObject{
            {QStringLiteral("path"), drawingPath}, {QStringLiteral("mode"), QStringLiteral("drafting")}});
        QVERIFY(!reopenRequest.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(reopenedResponses.count(), 1, 120000);
        const QList<QVariant> reopenResponse = reopenedResponses.at(0);
        QCOMPARE(reopenResponse.at(0).toString(), reopenRequest);
        QVERIFY2(reopenResponse.at(1).toBool(), qPrintable(reopenResponse.at(4).toString()));
        const QJsonObject reopened = qvariant_cast<QJsonObject>(reopenResponse.at(2));
        QCOMPARE(reopened.value(QStringLiteral("drawing_scene")).toObject(), committedScene);
        QVERIFY(!reopened.value(QStringLiteral("modified")).toBool());
        QVERIFY(drawing.open(QIODevice::ReadOnly));
        QCOMPARE(drawing.readAll(), savedBytes);
        drawing.close();
        process.stop();
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), ServiceProcess::State::Stopped, 15000);

        fixtureFinished.clear();
        fixture.start(julia, QStringList{
            QStringLiteral("--startup-file=no"),
            QStringLiteral("--project=") + projectDirectory.absolutePath(), fixtureScript,
            QStringLiteral("--verify-exact-line"), drawingPath});
        QTRY_COMPARE_WITH_TIMEOUT(fixtureFinished.count(), 1, 120000);
        const QByteArray verificationErrors = fixture.readAllStandardError();
        QVERIFY2(fixture.exitStatus() == QProcess::NormalExit && fixture.exitCode() == 0,
                 verificationErrors.constData());
    }
};

QTEST_MAIN(JuliaDraftingIntegrationTests)
#include "julia_drafting_integration_tests.moc"
