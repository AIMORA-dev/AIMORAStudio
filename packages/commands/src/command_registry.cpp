// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "aimora/studio/commands/command_registry.hpp"

#include <QChar>

#include <algorithm>
#include <utility>

namespace aimora::studio::commands {

bool CommandDefinition::isValid() const {
    return !id.isEmpty() && id.trimmed() == id && !id.contains(QChar{u' '})
        && !label.trimmed().isEmpty() && !category.trimmed().isEmpty();
}

RegistrationResult CommandRegistry::registerCommand(CommandDefinition definition) {
    if(!definition.isValid()) {
        return RegistrationResult::InvalidDefinition;
    }
    if(commands_.contains(definition.id)) {
        return RegistrationResult::DuplicateId;
    }

    commands_.insert(definition.id, std::move(definition));
    return RegistrationResult::Added;
}

bool CommandRegistry::contains(QStringView id) const {
    return commands_.contains(id.toString());
}

std::optional<CommandDefinition> CommandRegistry::find(QStringView id) const {
    const auto iterator = commands_.constFind(id.toString());
    if(iterator == commands_.constEnd()) {
        return std::nullopt;
    }
    return iterator.value();
}

QList<CommandDefinition> CommandRegistry::orderedCommands() const {
    QList<CommandDefinition> definitions = commands_.values();
    std::sort(definitions.begin(), definitions.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return definitions;
}

qsizetype CommandRegistry::size() const noexcept {
    return commands_.size();
}

} // namespace aimora::studio::commands
