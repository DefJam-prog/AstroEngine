#pragma once

#include "astro/core/ConfigurationService.hpp"
#include "astro/core/Logger.hpp"

namespace astro::core
{
class EngineContext
{
public:
    EngineContext() = default;
    explicit EngineContext(LogSink sink) : logger_(std::move(sink)) {}

    Logger& logger() noexcept { return logger_; }
    const Logger& logger() const noexcept { return logger_; }
    ConfigurationService& configuration() noexcept { return configuration_; }
    const ConfigurationService& configuration() const noexcept { return configuration_; }

private:
    Logger logger_;
    ConfigurationService configuration_;
};
}
