// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace aimora::studio::commands {

struct CommandDefinition final {
    QString id;
    QString label;
    QString category;
    QKeySequence defaultShortcut;

    [[nodiscard]] bool isValid() const;
};

enum class RegistrationResult {
    Added,
    DuplicateId,
    InvalidDefinition,
};

class CommandRegistry final {
public:
    [[nodiscard]] RegistrationResult registerCommand(CommandDefinition definition);
    [[nodiscard]] bool contains(QStringView id) const;
    [[nodiscard]] std::optional<CommandDefinition> find(QStringView id) const;
    [[nodiscard]] QList<CommandDefinition> orderedCommands() const;
    [[nodiscard]] qsizetype size() const noexcept;

private:
    QHash<QString, CommandDefinition> commands_;
};

} // namespace aimora::studio::commands
