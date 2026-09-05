// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QKeyEvent>
#include <QLineEdit>
#include <QObject>
#include <QStringList>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QApplication>
#include <QShortcut>

namespace aimora::studio::shell {

// Session-local input recall. QObject parenting ties its lifetime to the editor.
class CommandLineHistory final : public QObject {
  public:
    static constexpr qsizetype maximumEntries = 200;
    static constexpr qsizetype maximumEntryCharacters = 4096;

    explicit CommandLineHistory(QLineEdit& editor)
        : QObject{&editor}, editor_{editor} {
        editor_.installEventFilter(this);
        connect(&editor_, &QLineEdit::returnPressed, this, [this]() {
            const QString input = editor_.text();
            if(!input.trimmed().isEmpty() && input.size() <= maximumEntryCharacters
                && (entries_.isEmpty() || entries_.last() != input)) {
                if(entries_.size() == maximumEntries) {
                    entries_.removeFirst();
                }
                entries_.append(input);
            }
            cursor_ = entries_.size();
            draft_.clear();
            if(historyText_) {
                historyText_->setPlainText(entries_.join(QLatin1Char('\n')));
            }
        });
        connect(&editor_, &QLineEdit::textEdited, this, [this]() {
            cursor_ = entries_.size();
            draft_.clear();
        });
    }

    void showHistory() {
        if(historyWindow_) {
            historyWindow_->raise();
            historyWindow_->activateWindow();
            return;
        }
        auto* dialog = new QDialog{editor_.window(), Qt::Tool};
        const QPointer<QWidget> previousFocus = QApplication::focusWidget();
        connect(dialog, &QDialog::finished, this, [this, dialog, previousFocus](int) {
            // Closing schedules deletion; release the active window immediately so
            // another F2 press cannot reactivate a dialog awaiting destruction.
            if(historyWindow_ == dialog) {
                historyWindow_.clear();
                historyText_.clear();
            }
            if(previousFocus && previousFocus->isVisible()) {
                previousFocus->window()->activateWindow();
                previousFocus->setFocus(Qt::ShortcutFocusReason);
            }
        });
        dialog->setObjectName(QStringLiteral("aimora.command-history-window"));
        dialog->setWindowTitle(tr("Command input history"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(640, 420);
        auto* layout = new QVBoxLayout{dialog};
        auto* text = new QPlainTextEdit{dialog};
        text->setObjectName(QStringLiteral("aimora.command-history-text"));
        text->setAccessibleName(tr("Submitted command input"));
        text->setReadOnly(true);
        text->setMaximumBlockCount(static_cast<int>(maximumEntries));
        text->setPlainText(entries_.join(QLatin1Char('\n')));
        layout->addWidget(text);
        auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close, dialog};
        auto* clear = buttons->addButton(tr("Clear history"), QDialogButtonBox::ActionRole);
        clear->setObjectName(QStringLiteral("aimora.clear-command-history"));
        connect(clear, &QPushButton::clicked, this, [this]() {
            entries_.clear();
            cursor_ = 0;
            draft_.clear();
            if(historyText_) {
                historyText_->clear();
            }
        });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        auto* toggle = new QShortcut{QKeySequence{Qt::Key_F2}, dialog};
        connect(toggle, &QShortcut::activated, dialog, &QDialog::reject);
        layout->addWidget(buttons);
        historyWindow_ = dialog;
        historyText_ = text;
        dialog->show();
    }

    void toggleHistory() {
        if(historyWindow_ && historyWindow_->isVisible()) {
            historyWindow_->reject();
        } else {
            showHistory();
        }
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if(watched != &editor_ || event->type() != QEvent::KeyPress) {
            return QObject::eventFilter(watched, event);
        }
        const auto* key = static_cast<QKeyEvent*>(event);
        if(key->modifiers() != Qt::NoModifier || entries_.isEmpty()) {
            return QObject::eventFilter(watched, event);
        }
        if(key->key() == Qt::Key_Up) {
            if(cursor_ == entries_.size()) {
                draft_ = editor_.text();
            }
            if(cursor_ > 0) {
                --cursor_;
            }
            editor_.setText(entries_.at(cursor_));
        } else if(key->key() == Qt::Key_Down && cursor_ < entries_.size()) {
            ++cursor_;
            editor_.setText(cursor_ == entries_.size() ? draft_ : entries_.at(cursor_));
        } else {
            return QObject::eventFilter(watched, event);
        }
        editor_.setCursorPosition(editor_.text().size());
        return true;
    }

  private:
    QLineEdit& editor_;
    QStringList entries_;
    QString draft_;
    qsizetype cursor_{0};
    QPointer<QDialog> historyWindow_;
    QPointer<QPlainTextEdit> historyText_;
};

} // namespace aimora::studio::shell
