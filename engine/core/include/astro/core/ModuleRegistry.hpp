#pragma once

#include "astro/core/EngineContext.hpp"
#include "astro/core/IEngineModule.hpp"
#include "astro/core/Result.hpp"

#include <memory>
#include <string>
#include <vector>

namespace astro::core
{
class ModuleRegistry
{
public:
    explicit ModuleRegistry(EngineContext& context) noexcept : context_(context) {}

    Result<void> registerModule(std::unique_ptr<IEngineModule> module);
    Result<void> unregisterModule(const std::string& moduleName);
    Result<void> initializeAll();
    Result<void> tick(double fixedDeltaSeconds);
    Result<void> shutdownAll();

    [[nodiscard]] bool isRunning() const noexcept { return running_; }
    [[nodiscard]] std::vector<std::string> initializationOrder() const { return initializedNames_; }

private:
    Result<std::vector<std::size_t>> resolveOrder() const;
    void rollbackInitialized(std::string& diagnostic);

    EngineContext& context_;
    std::vector<std::unique_ptr<IEngineModule>> modules_;
    std::vector<std::size_t> initialized_;
    std::vector<std::string> initializedNames_;
    bool running_ = false;
    bool sealed_ = false;
};
}
