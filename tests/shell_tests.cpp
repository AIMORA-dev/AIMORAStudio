// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QPainter>
#include <QPair>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

namespace {

[[nodiscard]] QJsonObject loadFixture(const QString& mode) {
    const QString path = QStringLiteral(AIMORA_TEST_FIXTURE_DIR)
        + QStringLiteral("/theme-")
        + mode
        + QStringLiteral(".json");
    QFile fixture{path};
    if(!fixture.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(fixture.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

[[nodiscard]] QJsonObject tokenObject(
    const aimora::studio::themes::ThemeTokens& tokens
) {
    return QJsonObject{
        {QStringLiteral("window"), tokens.window.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("panel"), tokens.panel.name(QColor::HexRgb).toUpper()},
        {
            QStringLiteral("panel_alternate"),
            tokens.panelAlternate.name(QColor::HexRgb).toUpper()
        },
        {QStringLiteral("canvas"), tokens.canvas.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("grid_minor"), tokens.gridMinor.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("grid_major"), tokens.gridMajor.name(QColor::HexRgb).toUpper()},
        {
            QStringLiteral("text_primary"),
            tokens.textPrimary.name(QColor::HexRgb).toUpper()
        },
        {
            QStringLiteral("text_secondary"),
            tokens.textSecondary.name(QColor::HexRgb).toUpper()
        },
        {
            QStringLiteral("text_disabled"),
            tokens.textDisabled.name(QColor::HexRgb).toUpper()
        },
        {QStringLiteral("border"), tokens.border.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("accent"), tokens.accent.name(QColor::HexRgb).toUpper()},
        {
            QStringLiteral("accent_text"),
            tokens.accentText.name(QColor::HexRgb).toUpper()
        },
        {QStringLiteral("selection"), tokens.selection.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("focus"), tokens.focus.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("warning"), tokens.warning.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("error"), tokens.error.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("success"), tokens.success.name(QColor::HexRgb).toUpper()},
        {QStringLiteral("conductor"), tokens.conductor.name(QColor::HexRgb).toUpper()},
    };
}

[[nodiscard]] QImage renderWidget(QWidget& widget) {
    QImage image{widget.size(), QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    QPainter painter{&image};
    widget.render(&painter);
    return image;
}

} // namespace

class ShellTests final : public QObject {
    Q_OBJECT

private slots:
    void themeTokensMatchCommittedFixtures();
    void themePreferencePersists();
    void shellDefaultsToMenuAndCanvasOnly();
    void panelsShowPinFloatAndHide();
    void workspaceStateRoundTripsAndRejectsCorruption();
    void lightAndDarkCanvasFixturesRenderDifferently();
};

void ShellTests::themeTokensMatchCommittedFixtures() {
    using aimora::studio::themes::ThemeTokens;
    using aimora::studio::themes::contrastRatio;
    using aimora::studio::themes::darkThemeTokens;
    using aimora::studio::themes::lightThemeTokens;

    const QList<QPair<QString, ThemeTokens>> themes{
        {QStringLiteral("light"), lightThemeTokens()},
        {QStringLiteral("dark"), darkThemeTokens()},
    };

    for(const auto& [mode, tokens] : themes) {
        QVERIFY2(tokens.isValid(), qPrintable(mode + QStringLiteral(" theme is invalid")));

        const QJsonObject fixture = loadFixture(mode);
        QVERIFY2(!fixture.isEmpty(), qPrintable(mode + QStringLiteral(" fixture missing")));
        QCOMPARE(
            fixture.value(QStringLiteral("schema")).toString(),
            QStringLiteral("aimora.theme.tokens.v1")
        );
        QCOMPARE(fixture.value(QStringLiteral("mode")).toString(), mode);
        QVERIFY(
            tokenObject(tokens)
            == fixture.value(QStringLiteral("tokens")).toObject()
        );

        QVERIFY(contrastRatio(tokens.textPrimary, tokens.window) >= 4.5);
        QVERIFY(contrastRatio(tokens.textPrimary, tokens.panel) >= 4.5);
        QVERIFY(contrastRatio(tokens.textPrimary, tokens.canvas) >= 4.5);
        QVERIFY(contrastRatio(tokens.textSecondary, tokens.window) >= 4.5);
        QVERIFY(contrastRatio(tokens.accentText, tokens.accent) >= 4.5);
    }
}

void ShellTests::themePreferencePersists() {
    using aimora::studio::themes::ThemeController;
    using aimora::studio::themes::ThemeMode;
    using aimora::studio::themes::ThemeSettings;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));

    {
        QSettings settings{settingsPath, QSettings::IniFormat};
        ThemeSettings themeSettings{settings};
        ThemeController controller{*qApp, themeSettings};
        controller.setRequestedMode(ThemeMode::Dark);
        QCOMPARE(
            static_cast<int>(controller.requestedMode()),
            static_cast<int>(ThemeMode::Dark)
        );
        QCOMPARE(
            static_cast<int>(controller.effectiveMode()),
            static_cast<int>(ThemeMode::Dark)
        );
    }

    {
        QSettings settings{settingsPath, QSettings::IniFormat};
        ThemeSettings themeSettings{settings};
        ThemeController controller{*qApp, themeSettings};
        QCOMPARE(
            static_cast<int>(controller.requestedMode()),
            static_cast<int>(ThemeMode::Dark)
        );
        QCOMPARE(
            static_cast<int>(controller.effectiveMode()),
            static_cast<int>(ThemeMode::Dark)
        );
    }
}

void ShellTests::shellDefaultsToMenuAndCanvasOnly() {
    using aimora::studio::shell::StudioMainWindow;
    using aimora::studio::themes::ThemeController;
    using aimora::studio::themes::ThemeSettings;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings{
        directory.filePath(QStringLiteral("settings.ini")),
        QSettings::IniFormat
    };
    ThemeSettings themeSettings{settings};
    ThemeController controller{*qApp, themeSettings};
    StudioMainWindow window{controller, settings};

    window.resetWorkspace();
    window.resize(1280, 800);
    window.show();
    QCoreApplication::processEvents();

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
    QCOMPARE(window.menuTitles(), expectedMenus);
    QVERIFY(!window.menuBar()->actions().isEmpty());
    QVERIFY(window.centralWidget() == window.drawingWorkspace());
    QVERIFY(window.drawingWorkspace()->isVisible());
    QVERIFY(window.findChildren<QToolBar*>().isEmpty());
    QVERIFY(window.findChildren<QStatusBar*>().isEmpty());

    for(const auto* panel : window.panels()) {
        QVERIFY(!panel->isVisible());
        QVERIFY(!panel->isPinned());
    }
}

void ShellTests::panelsShowPinFloatAndHide() {
    using aimora::studio::shell::StudioMainWindow;
    using aimora::studio::themes::ThemeController;
    using aimora::studio::themes::ThemeSettings;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings{
        directory.filePath(QStringLiteral("settings.ini")),
        QSettings::IniFormat
    };
    ThemeSettings themeSettings{settings};
    ThemeController controller{*qApp, themeSettings};
    StudioMainWindow window{controller, settings};
    window.resetWorkspace();
    window.show();
    QCoreApplication::processEvents();

    QAction* inspectorAction =
        window.commandAction(QStringView{u"view.inspector"});
    auto* inspectorPanel = window.panel(QStringView{u"panel.inspector"});
    QVERIFY(inspectorAction != nullptr);
    QVERIFY(inspectorPanel != nullptr);
    QCOMPARE(inspectorAction->shortcut(), QKeySequence{QStringLiteral("F4")});

    inspectorAction->trigger();
    QCoreApplication::processEvents();
    QVERIFY(inspectorPanel->isVisible());

    inspectorPanel->setPinned(true);
    QVERIFY(inspectorPanel->isPinned());
    QVERIFY(
        !inspectorPanel->features().testFlag(QDockWidget::DockWidgetClosable)
    );

    inspectorPanel->setFloating(true);
    QCoreApplication::processEvents();
    QVERIFY(inspectorPanel->isFloating());

    inspectorAction->trigger();
    QCoreApplication::processEvents();
    QVERIFY(inspectorPanel->isVisible());
    QVERIFY(inspectorAction->isChecked());

    inspectorPanel->setPinned(false);
    QVERIFY(
        inspectorPanel->features().testFlag(QDockWidget::DockWidgetClosable)
    );
    inspectorAction->trigger();
    QCoreApplication::processEvents();
    QVERIFY(!inspectorPanel->isVisible());
}

void ShellTests::workspaceStateRoundTripsAndRejectsCorruption() {
    using aimora::studio::shell::StudioMainWindow;
    using aimora::studio::shell::WorkspaceRestoreStatus;
    using aimora::studio::themes::ThemeController;
    using aimora::studio::themes::ThemeSettings;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings{
        directory.filePath(QStringLiteral("settings.ini")),
        QSettings::IniFormat
    };
    ThemeSettings themeSettings{settings};
    ThemeController controller{*qApp, themeSettings};

    {
        StudioMainWindow first{controller, settings};
        first.resetWorkspace();
        first.show();
        QCoreApplication::processEvents();

        auto* inspector = first.panel(QStringView{u"panel.inspector"});
        QVERIFY(inspector != nullptr);
        inspector->show();
        inspector->setPinned(true);
        first.saveWorkspace();
    }

    {
        StudioMainWindow restored{controller, settings};
        restored.show();
        QCoreApplication::processEvents();
        QCOMPARE(
            static_cast<int>(restored.restoreStatus()),
            static_cast<int>(WorkspaceRestoreStatus::Restored)
        );
        auto* inspector = restored.panel(QStringView{u"panel.inspector"});
        QVERIFY(inspector != nullptr);
        QVERIFY(inspector->isVisible());
        QVERIFY(inspector->isPinned());
        QVERIFY(!restored.shouldStartMaximized());
    }

    settings.setValue(
        QStringLiteral("workspace/geometry"),
        QByteArray{"invalid geometry"}
    );
    settings.setValue(
        QStringLiteral("workspace/mainWindowState"),
        QByteArray{"invalid state"}
    );
    settings.sync();

    StudioMainWindow recovered{controller, settings};
    QCOMPARE(
        static_cast<int>(recovered.restoreStatus()),
        static_cast<int>(WorkspaceRestoreStatus::InvalidState)
    );
    for(const auto* panel : recovered.panels()) {
        QVERIFY(!panel->isVisible());
        QVERIFY(!panel->isPinned());
    }
    QVERIFY(recovered.shouldStartMaximized());
}

void ShellTests::lightAndDarkCanvasFixturesRenderDifferently() {
    using aimora::studio::shell::StudioMainWindow;
    using aimora::studio::themes::ThemeController;
    using aimora::studio::themes::ThemeMode;
    using aimora::studio::themes::ThemeSettings;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings{
        directory.filePath(QStringLiteral("settings.ini")),
        QSettings::IniFormat
    };
    ThemeSettings themeSettings{settings};
    ThemeController controller{*qApp, themeSettings};
    StudioMainWindow window{controller, settings};
    window.resetWorkspace();
    window.resize(960, 640);
    window.show();
    QCoreApplication::processEvents();

    controller.setRequestedMode(ThemeMode::Light);
    QCoreApplication::processEvents();
    QImage lightImage = renderWidget(*window.drawingWorkspace());
    QCOMPARE(
        lightImage.pixelColor(QPoint{13, 13}),
        controller.tokens().canvas
    );

    controller.setRequestedMode(ThemeMode::Dark);
    QCoreApplication::processEvents();
    QImage darkImage = renderWidget(*window.drawingWorkspace());
    QCOMPARE(
        darkImage.pixelColor(QPoint{13, 13}),
        controller.tokens().canvas
    );

    QVERIFY(lightImage != darkImage);
}

QTEST_MAIN(ShellTests)

#include "shell_tests.moc"
