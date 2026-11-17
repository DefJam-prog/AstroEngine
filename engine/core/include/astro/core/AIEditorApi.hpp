#pragma once

#include "astro/core/CommandRegistry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace astro::core
{
struct AIEditorApiVersion
{
    std::uint32_t major = 1;
    std::uint32_t minor = 0;
};

// In-process typed facade for editor integrations. This is not a process or
// network security boundary; callers must already be trusted editor code.
class AIEditorApi final
{
public:
    explicit AIEditorApi(CommandRegistry& commands) noexcept : commands_(commands) {}

    [[nodiscard]] AIEditorApiVersion version() const noexcept { return {}; }
    [[nodiscard]] std::vector<RegisteredCommandSchema> commandCatalog() const
    {
        return commands_.commandCatalog();
    }

    template <typename Payload>
    Result<void> validate(const std::string& id, std::uint32_t schemaVersion, const Payload& payload)
    {
        return commands_.validate(id, schemaVersion, payload);
    }

    template <typename Payload, typename Output>
    Result<Output> execute(const std::string& id, std::uint32_t schemaVersion, const Payload& payload)
    {
        return commands_.template execute<Payload, Output>(id, schemaVersion, payload);
    }

private:
    CommandRegistry& commands_;
};
}
