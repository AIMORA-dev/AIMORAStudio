// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/canvas/viewport_state.hpp"
#include "aimora/studio/commands/command_registry.hpp"
#include "aimora/studio/core/application_info.hpp"
#include "aimora/studio/inspector/panel_state.hpp"
#include "aimora/studio/protocol/client_configuration.hpp"
#include "aimora/studio/themes/theme_mode.hpp"

#include <QByteArray>
#include <QKeySequence>
#include <QSizeF>
#include <QString>
#include <QtTest>

#include <limits>

class FoundationTests final : public QObject {
    Q_OBJECT

private slots:
    void applicationInfoMatchesDependencyLock();
    void commandRegistryRejectsInvalidAndDuplicateCommands();
    void themeModesRoundTrip();
    void viewportStateValidatesAndProjects();
    void inspectorPanelStatePreservesVisibilityRules();
    void protocolConfigurationRequiresBoundedAuthenticatedLocalState();
};

void FoundationTests::applicationInfoMatchesDependencyLock() {
    using aimora::studio::core::ApplicationInfo;

    QCOMPARE(ApplicationInfo::productName(), QStringLiteral("AIMORAStudio"));
    QCOMPARE(ApplicationInfo::requiredQtVersion(), QStringLiteral("6.11.2"));
    QCOMPARE(ApplicationInfo::runtimeQtVersion(), ApplicationInfo::requiredQtVersion());
    QVERIFY(ApplicationInfo::architectureSummary().contains(QStringLiteral("C++20")));
}

void FoundationTests::commandRegistryRejectsInvalidAndDuplicateCommands() {
    using aimora::studio::commands::CommandDefinition;
    using aimora::studio::commands::CommandRegistry;
    using aimora::studio::commands::RegistrationResult;

    CommandRegistry registry;
    const CommandDefinition invalid{QStringLiteral("file open"),
                                    QStringLiteral("Open"),
                                    QStringLiteral("File"),
                                    QKeySequence::Open};
    QCOMPARE(static_cast<int>(registry.registerCommand(invalid)),
             static_cast<int>(RegistrationResult::InvalidDefinition));

    const CommandDefinition open{QStringLiteral("file.open"),
                                 QStringLiteral("Open"),
                                 QStringLiteral("File"),
                                 QKeySequence::Open};
    QCOMPARE(static_cast<int>(registry.registerCommand(open)),
             static_cast<int>(RegistrationResult::Added));
    QCOMPARE(static_cast<int>(registry.registerCommand(open)),
             static_cast<int>(RegistrationResult::DuplicateId));
    QCOMPARE(registry.size(), qsizetype{1});
    QVERIFY(registry.contains(QStringView{u"file.open"}));
    QVERIFY(registry.find(QStringView{u"file.open"}).has_value());
}

void FoundationTests::themeModesRoundTrip() {
    using aimora::studio::themes::ThemeMode;
    using aimora::studio::themes::parseThemeMode;
    using aimora::studio::themes::toString;

    for(const ThemeMode mode : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark}) {
        const auto parsed = parseThemeMode(toString(mode));
        QVERIFY(parsed.has_value());
        QCOMPARE(static_cast<int>(*parsed), static_cast<int>(mode));
    }
    QVERIFY(!parseThemeMode(QStringView{u"unknown"}).has_value());
}

void FoundationTests::viewportStateValidatesAndProjects() {
    using aimora::studio::canvas::ViewportState;

    const ViewportState viewport{QPointF{10.0, 20.0}, 2.0};
    QVERIFY(viewport.isValid());
    const QRectF expected{QPointF{-40.0, -5.0}, QSizeF{100.0, 50.0}};
    QCOMPARE(viewport.visibleSceneRect(QSizeF{200.0, 100.0}), expected);

    const ViewportState invalid{QPointF{}, std::numeric_limits<double>::quiet_NaN()};
    QVERIFY(!invalid.isValid());
    QVERIFY(invalid.visibleSceneRect(QSizeF{200.0, 100.0}).isNull());
}

void FoundationTests::inspectorPanelStatePreservesVisibilityRules() {
    using aimora::studio::inspector::PanelPresentation;
    using aimora::studio::inspector::PanelState;

    const PanelState hidden{};
    QVERIFY(!hidden.isVisible());
    QVERIFY(hidden.isValid());

    const PanelState pinnedOverlay{PanelPresentation::Overlay, true, QSize{420, 720}};
    QVERIFY(pinnedOverlay.isVisible());
    QVERIFY(pinnedOverlay.isValid());

    const PanelState invalidHidden{PanelPresentation::Hidden, true, QSize{420, 720}};
    QVERIFY(!invalidHidden.isValid());
}

void FoundationTests::protocolConfigurationRequiresBoundedAuthenticatedLocalState() {
    using aimora::studio::protocol::ClientConfiguration;
    using aimora::studio::protocol::ClientLimits;

    const ClientConfiguration valid{
        .endpoint = QStringLiteral("aimora-session-001"),
        .sessionToken = QByteArray(32, 'a'),
        .limits = ClientLimits{},
    };
    QVERIFY(valid.isValid());

    ClientConfiguration missingToken = valid;
    missingToken.sessionToken.clear();
    QVERIFY(!missingToken.isValid());

    ClientConfiguration invalidLimits = valid;
    invalidLimits.limits.maxBinaryFrameBytes = 1;
    QVERIFY(!invalidLimits.isValid());
}

QTEST_GUILESS_MAIN(FoundationTests)

#include "foundation_tests.moc"
