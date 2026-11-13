#pragma once

#include "astro/core/EngineContext.hpp"
#include "astro/core/Result.hpp"

#include <string>
#include <vector>

namespace astro::core
{
class IEngineModule
{
public:
    virtual ~IEngineModule() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::vector<std::string> dependencies() const = 0;
    virtual Result<void> initialize(EngineContext& context) = 0;
    virtual Result<void> tick(EngineContext& context, double fixedDeltaSeconds) = 0;
    virtual Result<void> shutdown(EngineContext& context) = 0;
};
}
