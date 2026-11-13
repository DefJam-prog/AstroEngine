#include "astro/core/CommandRegistry.hpp"

#include <exception>

namespace astro::core
{
bool CommandRegistry::isValidId(const std::string& id) noexcept
{
    if (id.empty() || id.size() > 128) return false;
    bool segmentStart = true;
    for (const unsigned char character : id)
    {
        if (character == '.')
        {
            if (segmentStart) return false;
            segmentStart = true;
            continue;
        }
        const bool lowerCase = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        const bool underscore = character == '_';
        if (!(lowerCase || (!segmentStart && (digit || underscore)))) return false;
        segmentStart = false;
    }
    return !segmentStart;
}

Result<const CommandRegistry::ICommand*> CommandRegistry::find(const std::string& id, const std::uint32_t version,
    const std::type_index payloadType, const std::optional<std::type_index> outputType) const
{
    const auto found = commands_.find(id);
    if (found == commands_.end())
        return Result<const ICommand*>::failure({ErrorCode::NotFound, "Command is not registered: " + id});
    const auto& command = *found->second;
    if (command.version() != version)
        return Result<const ICommand*>::failure({ErrorCode::VersionMismatch,
            "Command schema version mismatch for '" + id + "'"});
    if (command.payloadType() != payloadType)
        return Result<const ICommand*>::failure({ErrorCode::TypeMismatch,
            "Command payload type mismatch for '" + id + "'"});
    if (outputType && command.outputType() != *outputType)
        return Result<const ICommand*>::failure({ErrorCode::TypeMismatch,
            "Command output type mismatch for '" + id + "'"});
    return Result<const ICommand*>::success(&command);
}

Result<void> CommandRegistry::validateRaw(const ICommand& command, const void* payload)
{
    try
    {
        return command.validate(payload);
    }
    catch (const std::exception& error)
    {
        return Result<void>::failure({ErrorCode::InternalError,
            std::string("Command validator threw: ") + error.what()});
    }
    catch (...)
    {
        return Result<void>::failure({ErrorCode::InternalError, "Command validator threw an unknown exception"});
    }
}

void CommandRegistry::record(const CommandAuditAction action, const std::string& id, const std::uint32_t version,
    std::optional<Error> error)
{
    CommandAuditEntry entry;
    entry.sequence = nextAuditSequence_++;
    entry.commandId = id;
    entry.version = version;
    entry.action = action;
    entry.succeeded = !error.has_value();
    entry.error = std::move(error);
    audit_.push_back(std::move(entry));
}
}
