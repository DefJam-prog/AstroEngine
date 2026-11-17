#pragma once

#include "astro/core/EngineContext.hpp"
#include "astro/core/Result.hpp"
#include "astro/core/TransactionHistory.hpp"

#include <any>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace astro::core
{
enum class CommandFieldType
{
    Utf8String,
    UnsignedInteger,
    Boolean,
    AssetId,
    StringOrUnsignedInteger
};

struct CommandFieldSchema
{
    std::string id;
    CommandFieldType type = CommandFieldType::Utf8String;
    bool required = true;
    std::string description;
};

struct CommandSchema
{
    std::string id;
    std::uint32_t version = 1;
    std::string description;
    std::vector<CommandFieldSchema> payloadFields;
    std::string outputType = "void";
};

struct RegisteredCommandSchema
{
    CommandSchema schema;
    bool transactional = false;
};

enum class CommandAuditAction
{
    Validate,
    Execute,
    Undo,
    Redo
};

struct CommandAuditEntry
{
    std::uint64_t sequence = 0;
    std::string commandId;
    std::uint32_t version = 0;
    CommandAuditAction action = CommandAuditAction::Validate;
    bool succeeded = false;
    std::optional<Error> error;
};

class CommandRegistry
{
public:
    explicit CommandRegistry(EngineContext& context) noexcept : context_(context) {}
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    template <typename Payload, typename Output>
    Result<void> registerCommand(CommandSchema schema,
        std::function<Result<void>(const Payload&)> validator,
        std::function<Result<Output>(EngineContext&, const Payload&)> executor)
    {
        static_assert(!std::is_reference_v<Payload>, "Command payload must be a value type");
        static_assert(!std::is_reference_v<Output>, "Command output must be a value type");
        static_assert(std::is_void_v<Output> || std::is_copy_constructible_v<Output>,
            "Command output must be copy constructible (or void)");
        if (!isValidId(schema.id) || schema.version == 0)
            return Result<void>::failure({ErrorCode::InvalidArgument, "Command ID or schema version is invalid"});
        if (!validator || !executor)
            return Result<void>::failure({ErrorCode::InvalidArgument, "Command validator and executor are required"});
        if (commands_.contains(schema.id))
            return Result<void>::failure({ErrorCode::AlreadyExists, "Command is already registered: " + schema.id});

        const auto id = schema.id;
        commands_.emplace(id, std::make_unique<CommandModel<Payload, Output>>(
            std::move(schema), std::move(validator), std::move(executor),
            std::function<Result<Output>(EngineContext&, const Payload&, CommandTransaction&)>{}));
        return Result<void>::success();
    }

    template <typename Payload, typename Output>
    Result<void> registerTransactionalCommand(CommandSchema schema,
        std::function<Result<void>(const Payload&)> validator,
        std::function<Result<Output>(EngineContext&, const Payload&, CommandTransaction&)> executor)
    {
        static_assert(!std::is_reference_v<Payload>, "Command payload must be a value type");
        static_assert(!std::is_reference_v<Output>, "Command output must be a value type");
        static_assert(std::is_void_v<Output> || std::is_copy_constructible_v<Output>,
            "Command output must be copy constructible (or void)");
        if (!isValidId(schema.id) || schema.version == 0)
            return Result<void>::failure({ErrorCode::InvalidArgument, "Command ID or schema version is invalid"});
        if (!validator || !executor)
            return Result<void>::failure({ErrorCode::InvalidArgument, "Transactional command validator and executor are required"});
        if (commands_.contains(schema.id))
            return Result<void>::failure({ErrorCode::AlreadyExists, "Command is already registered: " + schema.id});

        const auto id = schema.id;
        commands_.emplace(id, std::make_unique<CommandModel<Payload, Output>>(
            std::move(schema), std::move(validator),
            std::function<Result<Output>(EngineContext&, const Payload&)>{}, std::move(executor)));
        return Result<void>::success();
    }

    template <typename Payload>
    Result<void> validate(const std::string& id, std::uint32_t version, const Payload& payload)
    {
        const auto command = find(id, version, std::type_index(typeid(Payload)), std::nullopt);
        if (!command)
        {
            record(CommandAuditAction::Validate, id, version, command.error());
            return Result<void>::failure(command.error());
        }
        auto result = validateRaw(*command.value(), &payload);
        record(CommandAuditAction::Validate, id, version, result ? std::nullopt : std::optional<Error>(result.error()));
        return result;
    }

    template <typename Payload, typename Output>
    Result<Output> execute(const std::string& id, std::uint32_t version, const Payload& payload)
    {
        const auto command = find(id, version, std::type_index(typeid(Payload)), std::type_index(typeid(Output)));
        if (!command)
        {
            record(CommandAuditAction::Execute, id, version, command.error());
            return Result<Output>::failure(command.error());
        }
        auto validation = validateRaw(*command.value(), &payload);
        if (!validation)
        {
            record(CommandAuditAction::Execute, id, version, validation.error());
            return Result<Output>::failure(validation.error());
        }

        const bool transactional = command.value()->isTransactional();
        std::optional<CommandTransaction> transaction;
        if (transactional) transaction.emplace(id, id, version);
        else history_.clear(); // Non-transactional commands form an undo barrier, even if execution later fails.

        Result<std::any> executed = Result<std::any>::failure({ErrorCode::InternalError, "Command executor did not return"});
        try
        {
            executed = command.value()->execute(context_, &payload, transaction ? &*transaction : nullptr);
        }
        catch (const std::exception& error)
        {
            executed = Result<std::any>::failure({ErrorCode::InternalError,
                "Command '" + id + "' executor threw: " + error.what()});
        }
        catch (...)
        {
            executed = Result<std::any>::failure({ErrorCode::InternalError,
                "Command '" + id + "' executor threw an unknown exception"});
        }
        if (!executed)
        {
            record(CommandAuditAction::Execute, id, version, executed.error());
            return Result<Output>::failure(executed.error());
        }

        if (transaction)
        {
            auto committed = history_.commit(std::move(*transaction));
            if (!committed)
            {
                record(CommandAuditAction::Execute, id, version, committed.error());
                return Result<Output>::failure(committed.error());
            }
        }

        if constexpr (std::is_void_v<Output>)
        {
            record(CommandAuditAction::Execute, id, version, std::nullopt);
            return Result<void>::success();
        }
        else
        {
            try
            {
                auto value = std::any_cast<Output>(std::move(executed).value());
                record(CommandAuditAction::Execute, id, version, std::nullopt);
                return Result<Output>::success(std::move(value));
            }
            catch (const std::bad_any_cast&)
            {
                const Error error{ErrorCode::InternalError, "Command executor returned a value of the wrong type: " + id};
                record(CommandAuditAction::Execute, id, version, error);
                return Result<Output>::failure(error);
            }
        }
    }

    [[nodiscard]] std::vector<CommandAuditEntry> auditTrail() const { return audit_; }
    [[nodiscard]] std::size_t commandCount() const noexcept { return commands_.size(); }
    [[nodiscard]] std::vector<RegisteredCommandSchema> commandCatalog() const;
    Result<void> undo()
    {
        const auto origin = history_.nextUndoOrigin();
        auto result = history_.undo();
        record(CommandAuditAction::Undo, origin ? origin->first : "history.undo", origin ? origin->second : 1,
            result ? std::nullopt : std::optional<Error>(result.error()));
        return result;
    }
    Result<void> redo()
    {
        const auto origin = history_.nextRedoOrigin();
        auto result = history_.redo();
        record(CommandAuditAction::Redo, origin ? origin->first : "history.redo", origin ? origin->second : 1,
            result ? std::nullopt : std::optional<Error>(result.error()));
        return result;
    }
    [[nodiscard]] bool canUndo() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history_.canRedo(); }
    [[nodiscard]] std::size_t undoCount() const noexcept { return history_.undoCount(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return history_.redoCount(); }
    void clearHistory() noexcept { history_.clear(); }

private:
    class ICommand
    {
    public:
        virtual ~ICommand() = default;
        [[nodiscard]] virtual std::uint32_t version() const noexcept = 0;
        [[nodiscard]] virtual const CommandSchema& schema() const noexcept = 0;
        [[nodiscard]] virtual std::type_index payloadType() const noexcept = 0;
        [[nodiscard]] virtual std::type_index outputType() const noexcept = 0;
        [[nodiscard]] virtual bool isTransactional() const noexcept = 0;
        virtual Result<void> validate(const void* payload) const = 0;
        virtual Result<std::any> execute(EngineContext& context, const void* payload, CommandTransaction* transaction) const = 0;
    };

    template <typename Payload, typename Output>
    class CommandModel final : public ICommand
    {
    public:
        CommandModel(CommandSchema schema,
            std::function<Result<void>(const Payload&)> validator,
            std::function<Result<Output>(EngineContext&, const Payload&)> executor,
            std::function<Result<Output>(EngineContext&, const Payload&, CommandTransaction&)> transactionalExecutor)
            : schema_(std::move(schema)), validator_(std::move(validator)), executor_(std::move(executor)),
              transactionalExecutor_(std::move(transactionalExecutor)) {}

        std::uint32_t version() const noexcept override { return schema_.version; }
        const CommandSchema& schema() const noexcept override { return schema_; }
        std::type_index payloadType() const noexcept override { return typeid(Payload); }
        std::type_index outputType() const noexcept override { return typeid(Output); }
        bool isTransactional() const noexcept override { return static_cast<bool>(transactionalExecutor_); }
        Result<void> validate(const void* payload) const override
        {
            return validator_(*static_cast<const Payload*>(payload));
        }
        Result<std::any> execute(EngineContext& context, const void* payload, CommandTransaction* transaction) const override
        {
            if (transactionalExecutor_ && !transaction)
                return Result<std::any>::failure({ErrorCode::InternalError, "Transactional command has no transaction"});
            if constexpr (std::is_void_v<Output>)
            {
                auto result = transactionalExecutor_
                    ? transactionalExecutor_(context, *static_cast<const Payload*>(payload), *transaction)
                    : executor_(context, *static_cast<const Payload*>(payload));
                if (!result) return Result<std::any>::failure(result.error());
                return Result<std::any>::success(std::any{});
            }
            else
            {
                auto result = transactionalExecutor_
                    ? transactionalExecutor_(context, *static_cast<const Payload*>(payload), *transaction)
                    : executor_(context, *static_cast<const Payload*>(payload));
                if (!result) return Result<std::any>::failure(result.error());
                return Result<std::any>::success(std::any(std::move(result).value()));
            }
        }

    private:
        CommandSchema schema_;
        std::function<Result<void>(const Payload&)> validator_;
        std::function<Result<Output>(EngineContext&, const Payload&)> executor_;
        std::function<Result<Output>(EngineContext&, const Payload&, CommandTransaction&)> transactionalExecutor_;
    };

    static bool isValidId(const std::string& id) noexcept;
    Result<const ICommand*> find(const std::string& id, std::uint32_t version,
        std::type_index payloadType, std::optional<std::type_index> outputType) const;
    static Result<void> validateRaw(const ICommand& command, const void* payload);
    void record(CommandAuditAction action, const std::string& id, std::uint32_t version,
        std::optional<Error> error);

    EngineContext& context_;
    std::unordered_map<std::string, std::unique_ptr<ICommand>> commands_;
    TransactionHistory history_;
    std::vector<CommandAuditEntry> audit_;
    std::uint64_t nextAuditSequence_ = 1;
};
}
