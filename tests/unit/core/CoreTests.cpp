#include "astro/core/ConfigurationService.hpp"
#include "astro/core/CommandRegistry.hpp"
#include "astro/core/EventBus.hpp"
#include "astro/core/EngineContext.hpp"
#include "astro/core/Logger.hpp"
#include "astro/core/ModuleRegistry.hpp"
#include "astro/core/Result.hpp"
#include "astro/core/TimeService.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TestModule final : public astro::core::IEngineModule
{
public:
    TestModule(std::string name, std::vector<std::string> dependencies, std::vector<std::string>& events,
        bool failInitialization = false, bool failShutdown = false, bool failTick = false)
        : name_(std::move(name)), dependencies_(std::move(dependencies)), events_(events),
          failInitialization_(failInitialization), failShutdown_(failShutdown), failTick_(failTick) {}

    std::string name() const override { return name_; }
    std::vector<std::string> dependencies() const override { return dependencies_; }
    astro::core::Result<void> initialize(astro::core::EngineContext&) override
    {
        events_.push_back("init:" + name_);
        if (failInitialization_)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InternalError, "requested init failure"});
        return astro::core::Result<void>::success();
    }
    astro::core::Result<void> tick(astro::core::EngineContext&, double deltaSeconds) override
    {
        events_.push_back("tick:" + name_ + ":" + std::to_string(deltaSeconds));
        if (failTick_)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InternalError, "requested tick failure"});
        return astro::core::Result<void>::success();
    }
    astro::core::Result<void> shutdown(astro::core::EngineContext&) override
    {
        events_.push_back("stop:" + name_);
        if (failShutdown_)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InternalError, "requested shutdown failure"});
        return astro::core::Result<void>::success();
    }

private:
    std::string name_;
    std::vector<std::string> dependencies_;
    std::vector<std::string>& events_;
    bool failInitialization_;
    bool failShutdown_;
    bool failTick_;
};

std::unique_ptr<astro::core::IEngineModule> module(const std::string& name, std::vector<std::string> dependencies,
    std::vector<std::string>& events, bool failInitialization = false, bool failShutdown = false, bool failTick = false)
{
    return std::make_unique<TestModule>(name, std::move(dependencies), events, failInitialization, failShutdown, failTick);
}

void testResult()
{
    using namespace astro::core;
    auto value = Result<int>::success(42);
    check(value && value.value() == 42, "Result<T> success carries value");
    auto error = Result<int>::failure({ErrorCode::InvalidArgument, "bad"});
    check(!error && error.error().code == ErrorCode::InvalidArgument, "Result<T> failure carries typed error");
    auto done = Result<void>::success();
    auto failed = Result<void>::failure({ErrorCode::IoError, "io"});
    check(done.hasValue() && !failed && failed.error().message == "io", "Result<void> handles success and failure");
}

void testLogger()
{
    using namespace astro::core;
    std::vector<std::string> messages;
    Logger logger([&](LogLevel, std::string_view text) { messages.emplace_back(text); });
    logger.debug("filtered");
    logger.info("visible");
    logger.setThreshold(LogLevel::Warning);
    logger.info("filtered again");
    logger.error("visible error");
    logger.setThreshold(LogLevel::Off);
    logger.error("off");
    check(messages.size() == 2 && messages[0] == "visible" && messages[1] == "visible error",
        "Logger threshold filters messages");
}

void testConfiguration()
{
    using namespace astro::core;
    const auto dir = std::filesystem::temp_directory_path() / "AstroEngine-core-tests";
    std::filesystem::create_directories(dir);
    const auto valid = dir / "settings.ini";
    const auto malformed = dir / "malformed.ini";
    const auto duplicate = dir / "duplicate.ini";
    {
        std::ofstream file(valid, std::ios::binary);
        file << "\xEF\xBB\xBF# UTF-8 config\r\n window.width = 1280 \r\nname=Astro Engine\n";
    }
    { std::ofstream file(malformed, std::ios::binary); file << "not-a-setting\n"; }
    { std::ofstream file(duplicate, std::ios::binary); file << "key=one\nkey=two\n"; }

    ConfigurationService config;
    check(config.load(valid).hasValue(), "Configuration loads UTF-8, BOM, comments and CRLF");
    const auto width = config.get("window.width");
    check(width && width.value() == "1280", "Configuration trims keys and values");
    check(!config.get("missing") && config.get("missing").error().code == ErrorCode::NotFound,
        "Missing configuration key returns NotFound");
    check(!config.set("", "value") && !config.set("bad=key", "value") && !config.set("key", "line\nbreak"),
        "Configuration rejects invalid key/value syntax");

    check(!config.load(malformed) && config.get("window.width"),
        "Parse failure leaves existing configuration unchanged");
    check(!config.load(duplicate) && config.load(duplicate).error().code == ErrorCode::AlreadyExists,
        "Duplicate keys return an explicit error");

    const auto saved = dir / "saved.ini";
    check(config.set("user.name", "Zheka") && config.save(saved), "Configuration set and save succeed");
    ConfigurationService restored;
    check(restored.load(saved) && restored.get("user.name").value() == "Zheka",
        "Configuration round-trips through UTF-8 text file");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    check(!ec, "Temporary test directory cleanup succeeds");
}

void testEngineContext()
{
    using namespace astro::core;
    int logged = 0;
    EngineContext context([&](LogLevel, std::string_view) { ++logged; });
    context.logger().info("context ready");
    auto result = context.configuration().set("mode", "headless");
    check(logged == 1 && result && context.configuration().get("mode").value() == "headless",
        "EngineContext owns usable logger and configuration services");
}

void testModuleOrderAndShutdown()
{
    using namespace astro::core;
    EngineContext context;
    ModuleRegistry registry(context);
    std::vector<std::string> events;
    check(registry.registerModule(module("renderer", {"platform"}, events)).hasValue(), "Register dependent module");
    check(registry.registerModule(module("platform", {}, events)).hasValue(), "Register dependency after dependent");
    check(registry.registerModule(module("input", {"platform"}, events)).hasValue(), "Register sibling dependency");
    check(registry.initializeAll().hasValue(), "Initialize dependency graph");
    check(registry.initializationOrder() == std::vector<std::string>({"platform", "renderer", "input"}),
        "Initialization order is deterministic and dependencies come first");
    check(registry.isRunning(), "Registry reports running after initialization");
    auto late = registry.registerModule(module("late", {}, events));
    check(!late && late.error().code == ErrorCode::InvalidState, "Registration after initialization is rejected");
    check(registry.shutdownAll().hasValue(), "Shutdown succeeds");
    check(events == std::vector<std::string>({"init:platform", "init:renderer", "init:input",
        "stop:input", "stop:renderer", "stop:platform"}), "Shutdown uses reverse initialization order");
    check(!registry.isRunning() && !registry.shutdownAll(), "Shutdown is explicit and cannot run twice");
    check(!registry.initializeAll() && !registry.registerModule(module("later", {}, events)),
        "A completed lifecycle cannot be restarted or mutated");
}

void testModuleDependencyDiagnostics()
{
    using namespace astro::core;
    {
        EngineContext context;
        ModuleRegistry registry(context);
        std::vector<std::string> events;
        check(registry.registerModule(module("needs", {"absent"}, events)).hasValue(), "Register module with missing dependency");
        auto result = registry.initializeAll();
        check(!result && result.error().code == ErrorCode::DependencyError
                && result.error().message.find("absent") != std::string::npos && events.empty(),
            "Missing dependencies are diagnosed before initialization");
        check(registry.unregisterModule("needs").hasValue(), "Invalid graph can be edited before retry");
    }
    {
        EngineContext context;
        ModuleRegistry registry(context);
        std::vector<std::string> events;
        check(registry.registerModule(module("a", {"b"}, events)).hasValue(), "Register first cyclic module");
        check(registry.registerModule(module("b", {"a"}, events)).hasValue(), "Register second cyclic module");
        auto result = registry.initializeAll();
        check(!result && result.error().code == ErrorCode::DependencyError
                && result.error().message.find("Cyclic") != std::string::npos && events.empty(),
            "Dependency cycles are diagnosed before initialization");
    }
    {
        EngineContext context;
        ModuleRegistry registry(context);
        std::vector<std::string> events;
        check(registry.registerModule(module("same", {}, events)).hasValue(), "Register initial module name");
        auto duplicate = registry.registerModule(module("same", {}, events));
        check(!duplicate && duplicate.error().code == ErrorCode::AlreadyExists, "Duplicate module names are rejected");
    }
}

void testModuleInitializationRollback()
{
    using namespace astro::core;
    EngineContext context;
    ModuleRegistry registry(context);
    std::vector<std::string> events;
    check(registry.registerModule(module("base", {}, events)).hasValue(), "Register base module");
    check(registry.registerModule(module("service", {"base"}, events)).hasValue(), "Register service module");
    check(registry.registerModule(module("broken", {"service"}, events, true)).hasValue(), "Register failing module");
    auto result = registry.initializeAll();
    check(!result && result.error().code == ErrorCode::InternalError
            && result.error().message.find("broken") != std::string::npos,
        "Initialization failure is returned with module context");
    check(events == std::vector<std::string>({"init:base", "init:service", "init:broken", "stop:service", "stop:base"}),
        "Already initialized modules roll back in reverse order");
    check(!registry.isRunning() && registry.initializationOrder().empty(), "Failed initialization leaves no modules running");
}

void testModuleShutdownContinuesAfterError()
{
    using namespace astro::core;
    EngineContext context;
    ModuleRegistry registry(context);
    std::vector<std::string> events;
    check(registry.registerModule(module("base", {}, events, false, true)).hasValue(), "Register shutdown-failing module");
    check(registry.registerModule(module("service", {"base"}, events)).hasValue(), "Register dependent for shutdown test");
    check(registry.initializeAll().hasValue(), "Initialize modules before shutdown error test");
    auto result = registry.shutdownAll();
    check(!result && result.error().message.find("base") != std::string::npos,
        "Shutdown errors are reported");
    check(events == std::vector<std::string>({"init:base", "init:service", "stop:service", "stop:base"}),
        "Shutdown continues through all modules after an error");
    check(!registry.isRunning(), "Registry returns to stopped state after shutdown error");
}

struct TestEvent { int value; };
struct OtherEvent { int value; };
struct AddPayload { int left; int right; };
struct OtherPayload { int value; };
struct ConfigPayload { std::string key; std::string value; };
struct SetStatePayload { int value; };

void testCommandRegistry()
{
    using namespace astro::core;
    EngineContext context;
    CommandRegistry registry(context);
    int executions = 0;
    auto registered = registry.registerCommand<AddPayload, int>(
        {"math.add", 1},
        [](const AddPayload& payload) -> Result<void> {
            if (payload.left < 0 || payload.right < 0)
                return Result<void>::failure({ErrorCode::ValidationError, "operands must be non-negative"});
            return Result<void>::success();
        },
        [&executions](EngineContext& commandContext, const AddPayload& payload) -> Result<int> {
            ++executions;
            auto result = commandContext.configuration().set("last.command", "math.add");
            if (!result) return Result<int>::failure(result.error());
            return Result<int>::success(payload.left + payload.right);
        });
    check(registered.hasValue() && registry.commandCount() == 1, "Register typed command schema");

    auto dryRun = registry.validate("math.add", 1, AddPayload{3, 4});
    check(dryRun.hasValue() && executions == 0, "Validation-only dry-run does not execute command");
    auto rejected = registry.execute<AddPayload, int>("math.add", 1, {-1, 2});
    check(!rejected && rejected.error().code == ErrorCode::ValidationError && executions == 0,
        "Invalid payload is rejected before executor runs");
    auto result = registry.execute<AddPayload, int>("math.add", 1, {3, 4});
    check(result && result.value() == 7 && executions == 1
            && context.configuration().get("last.command").value() == "math.add",
        "Typed command executes with EngineContext and returns typed result");

    auto missing = registry.execute<AddPayload, int>("math.missing", 1, {1, 2});
    auto oldVersion = registry.validate("math.add", 2, AddPayload{1, 2});
    auto wrongPayload = registry.execute<OtherPayload, int>("math.add", 1, {1});
    auto wrongOutput = registry.execute<AddPayload, std::string>("math.add", 1, {1, 2});
    check(!missing && missing.error().code == ErrorCode::NotFound, "Unknown command ID returns NotFound");
    check(!oldVersion && oldVersion.error().code == ErrorCode::VersionMismatch, "Wrong command version returns VersionMismatch");
    check(!wrongPayload && wrongPayload.error().code == ErrorCode::TypeMismatch, "Wrong payload type returns TypeMismatch");
    check(!wrongOutput && wrongOutput.error().code == ErrorCode::TypeMismatch, "Wrong output type returns TypeMismatch");

    auto duplicate = registry.registerCommand<AddPayload, int>(
        {"math.add", 2}, [](const AddPayload&) { return Result<void>::success(); },
        [](EngineContext&, const AddPayload&) { return Result<int>::success(0); });
    auto badId = registry.registerCommand<AddPayload, int>(
        {"Math..add", 1}, [](const AddPayload&) { return Result<void>::success(); },
        [](EngineContext&, const AddPayload&) { return Result<int>::success(0); });
    auto zeroVersion = registry.registerCommand<AddPayload, int>(
        {"math.zero", 0}, [](const AddPayload&) { return Result<void>::success(); },
        [](EngineContext&, const AddPayload&) { return Result<int>::success(0); });
    check(!duplicate && duplicate.error().code == ErrorCode::AlreadyExists
            && !badId && !zeroVersion,
        "Duplicate IDs and invalid schema IDs/versions are rejected");

    auto setConfig = registry.registerCommand<ConfigPayload, void>(
        {"project.set_config", 1},
        [](const ConfigPayload& payload) -> Result<void> {
            return payload.key.empty()
                ? Result<void>::failure({ErrorCode::ValidationError, "key is required"})
                : Result<void>::success();
        },
        [](EngineContext& commandContext, const ConfigPayload& payload) -> Result<void> {
            return commandContext.configuration().set(payload.key, payload.value);
        });
    auto noValue = registry.execute<ConfigPayload, void>("project.set_config", 1, {"quality", "high"});
    check(setConfig && noValue && context.configuration().get("quality").value() == "high",
        "Void commands execute successfully without manufacturing a result value");

    auto failingCommand = registry.registerCommand<AddPayload, int>(
        {"test.reported_error", 1},
        [](const AddPayload&) { return Result<void>::success(); },
        [](EngineContext&, const AddPayload&) {
            return Result<int>::failure({ErrorCode::IoError, "reported executor error"});
        });
    auto reportedError = registry.execute<AddPayload, int>("test.reported_error", 1, {1, 2});
    check(failingCommand && !reportedError && reportedError.error().code == ErrorCode::IoError,
        "Executor Result errors are preserved for callers");

    const auto audit = registry.auditTrail();
    check(audit.size() == 9 && audit.front().action == CommandAuditAction::Validate
            && audit.front().succeeded && audit.back().action == CommandAuditAction::Execute
            && !audit.back().succeeded && audit.back().error.has_value() && audit[1].error.has_value(),
        "Audit records validation, execution and failures in sequence");
}

void testCommandCallbackExceptions()
{
    using namespace astro::core;
    EngineContext context;
    CommandRegistry registry(context);
    auto validatorCommand = registry.registerCommand<AddPayload, int>(
        {"test.validator_throw", 1},
        [](const AddPayload&) -> Result<void> { throw std::runtime_error("validator boom"); },
        [](EngineContext&, const AddPayload&) { return Result<int>::success(1); });
    auto executorCommand = registry.registerCommand<AddPayload, int>(
        {"test.executor_throw", 1},
        [](const AddPayload&) { return Result<void>::success(); },
        [](EngineContext&, const AddPayload&) -> Result<int> { throw std::runtime_error("executor boom"); });
    auto validation = registry.validate("test.validator_throw", 1, AddPayload{1, 2});
    auto execution = registry.execute<AddPayload, int>("test.executor_throw", 1, {1, 2});
    check(validatorCommand.hasValue() && !validation && validation.error().code == ErrorCode::InternalError,
        "Validator exceptions become explicit errors");
    check(executorCommand.hasValue() && !execution && execution.error().code == ErrorCode::InternalError
            && execution.error().message.find("executor boom") != std::string::npos,
        "Executor exceptions become explicit errors");
}

void testTransactionHistory()
{
    using namespace astro::core;
    int state = 0;
    TransactionHistory history;
    CommandTransaction transaction("set pair");
    check(transaction.addStep("set first", [&]() -> Result<void> { state = 1; return Result<void>::success(); },
        [&]() -> Result<void> { state = 0; return Result<void>::success(); }).hasValue(), "Add first transaction step");
    check(transaction.addStep("set second", [&]() -> Result<void> { state = 2; return Result<void>::success(); },
        [&]() -> Result<void> { state = 1; return Result<void>::success(); }).hasValue(), "Add second transaction step");
    check(history.commit(std::move(transaction)).hasValue() && state == 2 && history.canUndo(),
        "Commit applies all transaction steps and records undo history");
    check(history.undo().hasValue() && state == 0 && history.canRedo(), "Undo applies inverse steps in reverse order");
    check(history.redo().hasValue() && state == 2 && history.canUndo(), "Redo reapplies steps in forward order");
    check(history.undo().hasValue() && history.redoCount() == 1, "Undo exposes redo branch");

    CommandTransaction branch("new branch");
    check(branch.addStep("set new value", [&]() -> Result<void> { state = 3; return Result<void>::success(); },
        [&]() -> Result<void> { state = 0; return Result<void>::success(); }).hasValue(), "Add new branch step");
    check(history.commit(std::move(branch)).hasValue() && state == 3 && !history.canRedo(),
        "Committing after undo discards the old redo branch");

    CommandTransaction failing("rollback on failure");
    check(failing.addStep("temporary change", [&]() -> Result<void> { state = 4; return Result<void>::success(); },
        [&]() -> Result<void> { state = 3; return Result<void>::success(); }).hasValue(), "Add rollback prefix step");
    check(failing.addStep("failing change", []() -> Result<void> {
        return Result<void>::failure({ErrorCode::IoError, "apply failed"});
    }, []() -> Result<void> { return Result<void>::success(); }).hasValue(), "Add failing transaction step");
    auto failedCommit = history.commit(std::move(failing));
    check(!failedCommit && failedCommit.error().code == ErrorCode::IoError && state == 3
            && history.undoCount() == 1,
        "Failed transaction rolls back applied steps and does not enter history");
}

void testTransactionRecoveryFailures()
{
    using namespace astro::core;
    int state = 0;
    bool failUndoFirst = false;
    bool failApplySecond = false;
    TransactionHistory history;
    CommandTransaction transaction("recoverable");
    check(transaction.addStep("first", [&]() -> Result<void> { state = 1; return Result<void>::success(); }, [&]() -> Result<void> {
        if (failUndoFirst) return Result<void>::failure({ErrorCode::IoError, "undo failure"});
        state = 0;
        return Result<void>::success();
    }).hasValue(), "Add first recovery step");
    check(transaction.addStep("second", [&]() -> Result<void> {
        if (failApplySecond) return Result<void>::failure({ErrorCode::IoError, "redo failure"});
        state = 2;
        return Result<void>::success();
    }, [&]() -> Result<void> { state = 1; return Result<void>::success(); }).hasValue(), "Add second recovery step");
    check(history.commit(std::move(transaction)).hasValue() && state == 2, "Commit recovery test transaction");

    failUndoFirst = true;
    auto undoError = history.undo();
    check(!undoError && undoError.error().message.find("undo failure") != std::string::npos
            && state == 2 && history.canUndo(),
        "Failed undo restores already reverted steps and keeps history aligned");

    failUndoFirst = false;
    check(history.undo().hasValue() && state == 0, "Undo succeeds after transient failure is removed");
    failApplySecond = true;
    auto redoError = history.redo();
    check(!redoError && redoError.error().message.find("redo failure") != std::string::npos
            && state == 0 && history.canRedo(),
        "Failed redo rolls back its successful prefix");
    failApplySecond = false;
    check(history.redo().hasValue() && state == 2, "Redo succeeds after transient failure is removed");
}

void testFailedRollbackClearsHistory()
{
    using namespace astro::core;
    int state = 0;
    TransactionHistory history;
    CommandTransaction baseline("baseline");
    check(baseline.addStep("baseline state", [&]() -> Result<void> {
        state = 1;
        return Result<void>::success();
    }, [&]() -> Result<void> {
        state = 0;
        return Result<void>::success();
    }).hasValue(), "Add baseline transaction step");
    check(history.commit(std::move(baseline)).hasValue() && history.canUndo(), "Commit baseline history");

    CommandTransaction failing("rollback failure");
    check(failing.addStep("partial", [&]() -> Result<void> {
        state = 2;
        return Result<void>::success();
    }, []() -> Result<void> {
        return Result<void>::failure({ErrorCode::IoError, "rollback unavailable"});
    }).hasValue(), "Add step with failing rollback");
    check(failing.addStep("fail apply", []() -> Result<void> {
        return Result<void>::failure({ErrorCode::IoError, "apply unavailable"});
    }, []() -> Result<void> { return Result<void>::success(); }).hasValue(), "Add failing apply step");
    auto result = history.commit(std::move(failing));
    check(!result && result.error().message.find("rollback unavailable") != std::string::npos
            && state == 2 && !history.canUndo() && !history.canRedo(),
        "Failed compensation is reported and invalidates prior history");
}

void testTransactionalCommandsAndBarrier()
{
    using namespace astro::core;
    EngineContext context;
    CommandRegistry registry(context);
    int state = 0;
    auto registered = registry.registerTransactionalCommand<SetStatePayload, int>(
        {"state.set", 1},
        [](const SetStatePayload&) { return Result<void>::success(); },
        [&state](EngineContext&, const SetStatePayload& payload, CommandTransaction& transaction) -> Result<int> {
            const auto previous = state;
            const auto next = payload.value;
            auto added = transaction.addStep("set state",
                [&state, next]() -> Result<void> { state = next; return Result<void>::success(); },
                [&state, previous]() -> Result<void> { state = previous; return Result<void>::success(); });
            if (!added) return Result<int>::failure(added.error());
            return Result<int>::success(next);
        });
    auto set = registry.execute<SetStatePayload, int>("state.set", 1, {42});
    check(registered && set && set.value() == 42 && state == 42 && registry.canUndo(),
        "Transactional command applies state and is undoable");
    check(registry.undo().hasValue() && state == 0 && registry.canRedo(), "CommandRegistry routes undo to history");
    check(registry.redo().hasValue() && state == 42, "CommandRegistry routes redo to history");

    AddPayload payload{2, 3};
    auto ordinary = registry.registerCommand<AddPayload, int>(
        {"math.add", 1}, [](const AddPayload&) { return Result<void>::success(); },
        [](EngineContext&, const AddPayload& value) { return Result<int>::success(value.left + value.right); });
    auto value = registry.execute<AddPayload, int>("math.add", 1, payload);
    check(ordinary && value && value.value() == 5 && !registry.canUndo() && !registry.canRedo(),
        "Ordinary command execution creates a history barrier");

    auto restoreHistory = registry.execute<SetStatePayload, int>("state.set", 1, {55});
    auto failingOrdinary = registry.registerCommand<AddPayload, int>(
        {"external.irreversible", 1}, [](const AddPayload&) { return Result<void>::success(); },
        [](EngineContext&, const AddPayload&) {
            return Result<int>::failure({ErrorCode::IoError, "external action failed"});
        });
    auto failedExternal = registry.execute<AddPayload, int>("external.irreversible", 1, {1, 1});
    check(restoreHistory && failingOrdinary && !failedExternal && !registry.canUndo(),
        "Failing non-transactional command remains an undo barrier");
    const auto audit = registry.auditTrail();
    check(audit.size() == 6 && audit[0].commandId == "state.set"
            && audit[1].action == CommandAuditAction::Undo && audit[1].commandId == "state.set"
            && audit[2].action == CommandAuditAction::Redo && audit[2].commandId == "state.set"
            && audit[3].action == CommandAuditAction::Execute && audit[3].commandId == "math.add"
            && audit[4].commandId == "state.set" && audit[5].commandId == "external.irreversible"
            && !audit[5].succeeded,
        "Command audit includes execute, undo, redo and undo-barrier actions");
    const auto refreshed = registry.execute<SetStatePayload, int>("state.set", 1, {66});
    registry.clearHistory();
    check(refreshed && !registry.canUndo() && !registry.canRedo(), "Explicit history clear removes undo and redo branches");
}

void testEventBus()
{
    using namespace astro::core;
    EventBus bus;
    std::vector<int> received;
    auto second = SubscriptionId{};
    bool mutatedSubscriptions = false;
    auto first = bus.subscribe<TestEvent>([&](const TestEvent& event) {
        received.push_back(event.value);
        if (!mutatedSubscriptions)
        {
            auto removed = bus.unsubscribe(second);
            check(removed.hasValue(), "Callback can unsubscribe during dispatch");
            auto added = bus.subscribe<TestEvent>([&](const TestEvent& late) { received.push_back(late.value * 10); });
            check(added.hasValue(), "Callback can subscribe during dispatch");
            mutatedSubscriptions = true;
        }
    });
    auto other = bus.subscribe<OtherEvent>([&](const OtherEvent& event) { received.push_back(event.value * 100); });
    auto removeMe = bus.subscribe<TestEvent>([&](const TestEvent& event) { received.push_back(-event.value); });
    second = removeMe.value();
    check(first && other && removeMe, "Typed event subscriptions succeed");
    check(bus.publish(TestEvent{2}).hasValue(), "Typed event publish succeeds");
    check(received == std::vector<int>({2}), "Unsubscribe takes effect immediately and new subscriptions wait until next publish");
    check(bus.publish(OtherEvent{3}).hasValue() && received.back() == 300, "Event types are dispatched independently");
    check(bus.publish(TestEvent{4}).hasValue() && received.size() == 4 && received[2] == 4 && received[3] == 40,
        "Later publishes use updated subscriber set in insertion order");
    auto repeatedUnsubscribe = bus.unsubscribe(second);
    check(!repeatedUnsubscribe && repeatedUnsubscribe.error().code == ErrorCode::NotFound,
        "Repeated unsubscribe returns NotFound");
}

void testEventBusCallbackErrors()
{
    using namespace astro::core;
    EventBus bus;
    int called = 0;
    auto throws = bus.subscribe<TestEvent>([](const TestEvent&) { throw std::runtime_error("callback boom"); });
    auto continues = bus.subscribe<TestEvent>([&](const TestEvent&) { ++called; });
    auto result = bus.publish(TestEvent{1});
    check(throws && continues && !result && result.error().code == ErrorCode::InternalError
            && result.error().message.find("callback boom") != std::string::npos,
        "Callback exception becomes an error result");
    check(called == 1, "Callback dispatch continues after a callback exception");
}

void testTimeService()
{
    using namespace astro::core;
    TimeService time;
    check(time.configure({0.1, 0.5, 2}).hasValue(), "TimeService accepts valid configuration");
    auto first = time.advance(0.25);
    check(first && first.value().fixedStepCount == 2 && first.value().interpolationAlpha > 0.49
            && first.value().interpolationAlpha < 0.51 && first.value().simulationTimeSeconds == 0.2,
        "Fixed timestep emits capped steps and interpolation remainder");
    auto catchup = time.advance(0.45);
    check(catchup && catchup.value().fixedStepCount == 2 && catchup.value().droppedTimeSeconds >= 0.19
            && catchup.value().droppedTimeSeconds <= 0.31 && catchup.value().interpolationAlpha >= 0.0
            && catchup.value().interpolationAlpha < 1.0 && time.totalDroppedTimeSeconds() >= 0.19,
        "Excess whole catch-up steps are dropped and reported");
    auto clamped = time.advance(0.8);
    check(clamped && clamped.value().frameDeltaSeconds == 0.5 && clamped.value().droppedTimeSeconds > 0.49,
        "Large frame delta is clamped and discarded time reported");
    check(!time.advance(-0.1) && !time.advance(std::numeric_limits<double>::infinity())
            && !time.configure({0.0, 0.5, 2}) && !time.configure({0.1, 0.05, 2})
            && !time.configure({0.1, 0.5, 0}),
        "Invalid deltas and configurations are rejected");

    time.setPaused(true);
    auto paused = time.advance(0.2);
    check(paused && paused.value().fixedStepCount == 0 && paused.value().simulationTimeSeconds == time.simulationTimeSeconds(),
        "Paused time does not advance simulation");
    time.setPaused(false);
    auto resumed = time.advance(0.1);
    check(resumed && resumed.value().fixedStepCount == 1, "Simulation resumes after unpausing");
    check(!time.stepOnce(), "Manual fixed step requires paused simulation");
    time.setPaused(true);
    const auto manualStep = time.stepOnce();
    check(manualStep && manualStep.value().fixedStepCount == 1
        && manualStep.value().simulationTimeSeconds == time.simulationTimeSeconds(),
        "Manual fixed step advances simulation time exactly once while paused");
}

void testModuleFixedTick()
{
    using namespace astro::core;
    EngineContext context;
    ModuleRegistry registry(context);
    std::vector<std::string> events;
    check(registry.registerModule(module("first", {}, events)).hasValue(), "Register first ticking module");
    check(registry.registerModule(module("second", {"first"}, events)).hasValue(), "Register second ticking module");
    check(!registry.tick(1.0 / 60.0), "Tick before initialization is rejected");
    check(registry.initializeAll().hasValue(), "Modules initialize before fixed ticks");
    check(!registry.tick(0.0) && !registry.tick(std::numeric_limits<double>::quiet_NaN()),
        "Invalid fixed deltas are rejected");
    check(registry.tick(0.02).hasValue() && events[2] == "tick:first:0.020000" && events[3] == "tick:second:0.020000",
        "Fixed tick updates modules in initialization order");
    check(registry.shutdownAll().hasValue(), "Shutdown modules after fixed tick");

    EngineContext failingContext;
    ModuleRegistry failingRegistry(failingContext);
    std::vector<std::string> failingEvents;
    check(failingRegistry.registerModule(module("bad", {}, failingEvents, false, false, true)).hasValue(),
        "Register tick-failing module");
    check(failingRegistry.initializeAll().hasValue(), "Tick failure module initializes");
    auto failedTick = failingRegistry.tick(0.01);
    check(!failedTick && failedTick.error().message.find("bad") != std::string::npos,
        "Module tick errors include module context");
    check(failingRegistry.shutdownAll().hasValue(), "Caller can shut down registry after tick error");
}
}

int main()
{
    testResult();
    testLogger();
    testConfiguration();
    testEngineContext();
    testModuleOrderAndShutdown();
    testModuleDependencyDiagnostics();
    testModuleInitializationRollback();
    testModuleShutdownContinuesAfterError();
    testEventBus();
    testEventBusCallbackErrors();
    testTimeService();
    testModuleFixedTick();
    testCommandRegistry();
    testCommandCallbackExceptions();
    testTransactionHistory();
    testTransactionRecoveryFailures();
    testFailedRollbackClearsHistory();
    testTransactionalCommandsAndBarrier();
    if (failures != 0) std::cerr << failures << " assertion(s) failed\n";
    else std::cout << "Core unit tests passed\n";
    return failures == 0 ? 0 : 1;
}
