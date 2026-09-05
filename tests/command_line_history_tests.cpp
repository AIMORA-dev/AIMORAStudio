// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/shell/command_line_history.hpp"
#include "aimora/studio/shell/studio_shell.hpp"
#include "aimora/studio/themes/theme_system.hpp"

#include <QTest>
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

using aimora::studio::shell::CommandLineHistory;

class CommandLineHistoryTests final : public QObject {
    Q_OBJECT

private slots:
    void historyReopensBeforeDeferredCloseIsProcessed() {
        QLineEdit editor;
        CommandLineHistory history{editor};
        history.showHistory();
        history.toggleHistory();
        history.toggleHistory();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto dialogs = editor.findChildren<QDialog*>();
        bool visibleHistory = false;
        for (const auto* dialog : dialogs) {
            visibleHistory = visibleHistory || dialog->isVisible();
        }
        QVERIFY(visibleHistory);
    }

    void historyToggleAndEscapeReleaseTheWindow() {
        QLineEdit editor;
        CommandLineHistory history{editor};
        history.toggleHistory();
        QPointer<QDialog> first = editor.findChild<QDialog*>(QStringLiteral("aimora.command-history-window"));
        QVERIFY(first);
        QVERIFY(first->isVisible());
        history.toggleHistory();
        QTRY_VERIFY(first.isNull());
        history.toggleHistory();
        QPointer<QDialog> second = editor.findChild<QDialog*>(QStringLiteral("aimora.command-history-window"));
        QVERIFY(second);
        QVERIFY(second->isVisible());
        QTest::keyClick(second, Qt::Key_Escape);
        QTRY_VERIFY(second.isNull());
    }

    void studioRecallDoesNotSubmitCommands() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QSettings settings{QDir{root.path()}.filePath(QStringLiteral("studio.ini")), QSettings::IniFormat};
        aimora::studio::themes::ThemeSettings themeSettings{settings};
        aimora::studio::themes::ThemeController themeController{*qApp, themeSettings};
        aimora::studio::shell::StudioMainWindow window{themeController, settings};
        auto* editor = window.findChild<QLineEdit*>(QStringLiteral("aimora.command-line"));
        QVERIFY(editor != nullptr);
        QSignalSpy submissions{editor, &QLineEdit::returnPressed};
        for(const QString& input : {QStringLiteral("z"), QStringLiteral("e")}) {
            QTest::keyClicks(editor, input);
            QTest::keyClick(editor, Qt::Key_Return);
            // History records submitted input, including a command rejected by an empty canvas.
            editor->clear();
        }
        QCOMPARE(submissions.count(), 2);
        editor->setText(QStringLiteral("@12,"));
        QTest::keyClick(editor, Qt::Key_Up);
        QCOMPARE(editor->text(), QStringLiteral("e"));
        QTest::keyClick(editor, Qt::Key_Up);
        QCOMPARE(editor->text(), QStringLiteral("z"));
        QTest::keyClick(editor, Qt::Key_Down);
        QTest::keyClick(editor, Qt::Key_Down);
        QCOMPARE(editor->text(), QStringLiteral("@12,"));
        QCOMPARE(submissions.count(), 2);
        QVERIFY(!window.isWindowModified());
        auto* historyAction = window.findChild<QAction*>(QStringLiteral("aimora.command-history"));
        QVERIFY(historyAction != nullptr);
        QCOMPARE(historyAction->shortcut(), QKeySequence{Qt::Key_F2});
        historyAction->trigger();
        auto* historyText = window.findChild<QPlainTextEdit*>(QStringLiteral("aimora.command-history-text"));
        QVERIFY(historyText != nullptr);
        QVERIFY(historyText->isReadOnly());
        QCOMPARE(historyText->toPlainText(), QStringLiteral("z\ne"));
        QCOMPARE(submissions.count(), 2);
        QVERIFY(!window.isWindowModified());
    }

    void historyWindowClearsRecallWithoutChangingCurrentInput() {
        QLineEdit editor;
        CommandLineHistory history{editor};
        editor.setText(QStringLiteral("circle"));
        QVERIFY(QMetaObject::invokeMethod(&editor, "returnPressed"));
        editor.setText(QStringLiteral("unfinished"));
        history.showHistory();
        auto* clear = editor.findChild<QPushButton*>(QStringLiteral("aimora.clear-command-history"));
        auto* text = editor.findChild<QPlainTextEdit*>(QStringLiteral("aimora.command-history-text"));
        QVERIFY(clear != nullptr);
        QVERIFY(text != nullptr);
        QCOMPARE(text->toPlainText(), QStringLiteral("circle"));
        clear->click();
        QVERIFY(text->toPlainText().isEmpty());
        QCOMPARE(editor.text(), QStringLiteral("unfinished"));
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.text(), QStringLiteral("unfinished"));
        editor.setText(QStringLiteral("rectangle"));
        QVERIFY(QMetaObject::invokeMethod(&editor, "returnPressed"));
        QCOMPARE(text->toPlainText(), QStringLiteral("rectangle"));
    }

    void recallPreservesUnsubmittedDraft() {
        QLineEdit editor;
        CommandLineHistory history{editor};
        for(const QString& input : {QStringLiteral("l"), QStringLiteral("@10,20")}) {
            editor.setText(input);
            QVERIFY(QMetaObject::invokeMethod(&editor, "returnPressed"));
            editor.clear();
        }
        editor.setText(QStringLiteral("unfinished"));
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.text(), QStringLiteral("@10,20"));
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.text(), QStringLiteral("l"));
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.text(), QStringLiteral("@10,20"));
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.text(), QStringLiteral("unfinished"));
    }

    void repeatedBlankAndOversizedInputsDoNotCrowdHistory() {
        QLineEdit editor;
        CommandLineHistory history{editor};
        for(const QString& input : {QStringLiteral("circle"), QStringLiteral("circle"),
                                   QStringLiteral(" "), QString(5000, QLatin1Char('x'))}) {
            editor.setText(input);
            QVERIFY(QMetaObject::invokeMethod(&editor, "returnPressed"));
        }
        editor.clear();
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.text(), QStringLiteral("circle"));
        QTest::keyClick(&editor, Qt::Key_Down);
        QVERIFY(editor.text().isEmpty());
    }

    void retainedHistoryIsBoundedAndEditingRestartsRecall() {
        QLineEdit editor;
        CommandLineHistory history{editor};
        for(qsizetype index = 0; index < CommandLineHistory::maximumEntries + 10; ++index) {
            editor.setText(QString::number(index));
            QVERIFY(QMetaObject::invokeMethod(&editor, "returnPressed"));
        }
        editor.clear();
        for(qsizetype index = 0; index < CommandLineHistory::maximumEntries + 10; ++index) {
            QTest::keyClick(&editor, Qt::Key_Up);
        }
        QCOMPARE(editor.text(), QStringLiteral("10"));
        QTest::keyClicks(&editor, "x");
        QCOMPARE(editor.text(), QStringLiteral("10x"));
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.text(), QString::number(CommandLineHistory::maximumEntries + 9));
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.text(), QStringLiteral("10x"));
    }
};

QTEST_MAIN(CommandLineHistoryTests)
#include "command_line_history_tests.moc"
