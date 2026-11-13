#include "astro/core/ModuleRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace astro::core
{
namespace
{
Error lifecycleError(const ErrorCode code, const std::string& message)
{
    return {code, message};
}

std::string exceptionMessage(const char* operation, const std::string& moduleName, const char* what)
{
    return std::string("Module '") + moduleName + "' " + operation + " threw: " + what;
}
}

Result<void> ModuleRegistry::registerModule(std::unique_ptr<IEngineModule> module)
{
    if (running_ || sealed_ || !initialized_.empty())
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidState, "Cannot change modules after initialization"));
    if (!module)
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidArgument, "Cannot register a null module"));

    std::string moduleName;
    try
    {
        moduleName = module->name();
    }
    catch (const std::exception& error)
    {
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidArgument,
            std::string("Reading module name threw: ") + error.what()));
    }
    catch (...)
    {
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidArgument, "Reading module name threw an unknown exception"));
    }

    if (moduleName.empty())
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidArgument, "Module name cannot be empty"));
    for (const auto& existing : modules_)
    {
        if (existing->name() == moduleName)
            return Result<void>::failure(lifecycleError(ErrorCode::AlreadyExists, "Module already registered: " + moduleName));
    }
    modules_.push_back(std::move(module));
    return Result<void>::success();
}

Result<void> ModuleRegistry::unregisterModule(const std::string& moduleName)
{
    if (running_ || sealed_ || !initialized_.empty())
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidState, "Cannot change modules after initialization"));
    const auto found = std::find_if(modules_.begin(), modules_.end(), [&](const auto& module) {
        return module->name() == moduleName;
    });
    if (found == modules_.end())
        return Result<void>::failure(lifecycleError(ErrorCode::NotFound, "Module is not registered: " + moduleName));
    modules_.erase(found);
    return Result<void>::success();
}

Result<std::vector<std::size_t>> ModuleRegistry::resolveOrder() const
{
    std::unordered_map<std::string, std::size_t> byName;
    try
    {
        for (std::size_t index = 0; index < modules_.size(); ++index)
            byName.emplace(modules_[index]->name(), index);
    }
    catch (const std::exception& error)
    {
        return Result<std::vector<std::size_t>>::failure(
            lifecycleError(ErrorCode::InvalidArgument, std::string("Reading module metadata threw: ") + error.what()));
    }
    catch (...)
    {
        return Result<std::vector<std::size_t>>::failure(
            lifecycleError(ErrorCode::InvalidArgument, "Reading module metadata threw an unknown exception"));
    }

    enum class Mark { Unvisited, Visiting, Visited };
    std::vector<Mark> marks(modules_.size(), Mark::Unvisited);
    std::vector<std::size_t> order;
    order.reserve(modules_.size());
    std::function<Result<void>(std::size_t)> visit = [&](const std::size_t index) -> Result<void> {
        if (marks[index] == Mark::Visited) return Result<void>::success();
        const auto moduleName = modules_[index]->name();
        if (marks[index] == Mark::Visiting)
            return Result<void>::failure(lifecycleError(ErrorCode::DependencyError, "Cyclic module dependency at: " + moduleName));
        marks[index] = Mark::Visiting;

        std::vector<std::string> required;
        try
        {
            required = modules_[index]->dependencies();
        }
        catch (const std::exception& error)
        {
            return Result<void>::failure(lifecycleError(ErrorCode::InvalidArgument,
                std::string("Reading dependencies for '") + moduleName + "' threw: " + error.what()));
        }
        catch (...)
        {
            return Result<void>::failure(lifecycleError(ErrorCode::InvalidArgument,
                "Reading dependencies for '" + moduleName + "' threw an unknown exception"));
        }
        std::unordered_set<std::string> uniqueDependencies;
        for (const auto& dependency : required)
        {
            if (dependency.empty() || !uniqueDependencies.insert(dependency).second)
                return Result<void>::failure(lifecycleError(ErrorCode::DependencyError,
                    "Module '" + moduleName + "' has an empty or duplicate dependency"));
            const auto found = byName.find(dependency);
            if (found == byName.end())
                return Result<void>::failure(lifecycleError(ErrorCode::DependencyError,
                    "Module '" + moduleName + "' requires missing module '" + dependency + "'"));
            if (found->second == index)
                return Result<void>::failure(lifecycleError(ErrorCode::DependencyError,
                    "Module '" + moduleName + "' cannot depend on itself"));
            auto dependencyResult = visit(found->second);
            if (!dependencyResult) return dependencyResult;
        }
        marks[index] = Mark::Visited;
        order.push_back(index);
        return Result<void>::success();
    };

    for (std::size_t index = 0; index < modules_.size(); ++index)
    {
        auto result = visit(index);
        if (!result) return Result<std::vector<std::size_t>>::failure(result.error());
    }
    return Result<std::vector<std::size_t>>::success(std::move(order));
}

void ModuleRegistry::rollbackInitialized(std::string& diagnostic)
{
    for (auto iterator = initialized_.rbegin(); iterator != initialized_.rend(); ++iterator)
    {
        const auto name = modules_[*iterator]->name();
        try
        {
            auto result = modules_[*iterator]->shutdown(context_);
            if (!result) diagnostic += "\nRollback shutdown of '" + name + "' failed: " + result.error().message;
        }
        catch (const std::exception& error)
        {
            diagnostic += "\n" + exceptionMessage("rollback shutdown", name, error.what());
        }
        catch (...)
        {
            diagnostic += "\nModule '" + name + "' rollback shutdown threw an unknown exception";
        }
    }
    initialized_.clear();
    initializedNames_.clear();
}

Result<void> ModuleRegistry::initializeAll()
{
    if (running_ || sealed_ || !initialized_.empty())
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidState, "Module registry has already been initialized"));
    auto resolved = resolveOrder();
    if (!resolved) return Result<void>::failure(resolved.error());

    for (const auto index : resolved.value())
    {
        const auto name = modules_[index]->name();
        Result<void> result = Result<void>::success();
        try
        {
            result = modules_[index]->initialize(context_);
        }
        catch (const std::exception& error)
        {
            result = Result<void>::failure(lifecycleError(ErrorCode::InternalError, exceptionMessage("initialization", name, error.what())));
        }
        catch (...)
        {
            result = Result<void>::failure(lifecycleError(ErrorCode::InternalError,
                "Module '" + name + "' initialization threw an unknown exception"));
        }
        if (!result)
        {
            std::string diagnostic = "Module '" + name + "' initialization failed: " + result.error().message;
            rollbackInitialized(diagnostic);
            return Result<void>::failure(lifecycleError(result.error().code, diagnostic));
        }
        initialized_.push_back(index);
        initializedNames_.push_back(name);
    }
    running_ = true;
    sealed_ = true;
    return Result<void>::success();
}

Result<void> ModuleRegistry::shutdownAll()
{
    if (!running_)
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidState, "Module registry is not running"));
    std::ostringstream failures;
    bool failed = false;
    for (auto iterator = initialized_.rbegin(); iterator != initialized_.rend(); ++iterator)
    {
        const auto name = modules_[*iterator]->name();
        try
        {
            auto result = modules_[*iterator]->shutdown(context_);
            if (!result)
            {
                failures << (failed ? "\n" : "") << "Module '" << name << "' shutdown failed: " << result.error().message;
                failed = true;
            }
        }
        catch (const std::exception& error)
        {
            failures << (failed ? "\n" : "") << exceptionMessage("shutdown", name, error.what());
            failed = true;
        }
        catch (...)
        {
            failures << (failed ? "\n" : "") << "Module '" << name << "' shutdown threw an unknown exception";
            failed = true;
        }
    }
    initialized_.clear();
    initializedNames_.clear();
    running_ = false;
    if (failed) return Result<void>::failure(lifecycleError(ErrorCode::InternalError, failures.str()));
    return Result<void>::success();
}

Result<void> ModuleRegistry::tick(const double fixedDeltaSeconds)
{
    if (!running_)
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidState, "Module registry is not running"));
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0)
        return Result<void>::failure(lifecycleError(ErrorCode::InvalidArgument, "Fixed delta must be finite and positive"));

    for (const auto index : initialized_)
    {
        const auto name = modules_[index]->name();
        Result<void> result = Result<void>::success();
        try
        {
            result = modules_[index]->tick(context_, fixedDeltaSeconds);
        }
        catch (const std::exception& error)
        {
            result = Result<void>::failure(lifecycleError(ErrorCode::InternalError, exceptionMessage("tick", name, error.what())));
        }
        catch (...)
        {
            result = Result<void>::failure(lifecycleError(ErrorCode::InternalError,
                "Module '" + name + "' tick threw an unknown exception"));
        }
        if (!result)
            return Result<void>::failure(lifecycleError(result.error().code, "Module '" + name + "' tick failed: " + result.error().message));
    }
    return Result<void>::success();
}
}
