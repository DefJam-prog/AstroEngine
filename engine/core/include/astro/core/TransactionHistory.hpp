#pragma once

#include "astro/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <utility>
#include <vector>

namespace astro::core
{
class CommandTransaction
{
public:
    using Action = std::function<Result<void>()>;

    explicit CommandTransaction(std::string label, std::string commandId = {}, std::uint32_t commandVersion = 0)
        : label_(std::move(label)), commandId_(std::move(commandId)), commandVersion_(commandVersion) {}
    Result<void> addStep(std::string label, Action apply, Action undo);
    [[nodiscard]] const std::string& label() const noexcept { return label_; }
    [[nodiscard]] std::size_t stepCount() const noexcept { return steps_.size(); }

private:
    struct Step
    {
        std::string label;
        Action apply;
        Action undo;
    };

    friend class TransactionHistory;
    std::string label_;
    std::string commandId_;
    std::uint32_t commandVersion_ = 0;
    std::vector<Step> steps_;
};

class TransactionHistory
{
public:
    Result<void> commit(CommandTransaction transaction);
    Result<void> undo();
    Result<void> redo();
    void clear() noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < history_.size(); }
    [[nodiscard]] std::size_t undoCount() const noexcept { return cursor_; }
    [[nodiscard]] std::size_t redoCount() const noexcept { return history_.size() - cursor_; }
    [[nodiscard]] std::optional<std::pair<std::string, std::uint32_t>> nextUndoOrigin() const;
    [[nodiscard]] std::optional<std::pair<std::string, std::uint32_t>> nextRedoOrigin() const;

private:
    static Result<void> invoke(const CommandTransaction::Action& action, const std::string& transactionLabel,
        const std::string& stepLabel, const char* direction);
    static Error appendRecoveryErrors(Error primary, const std::vector<Error>& recoveryErrors,
        const std::string& transactionLabel);

    std::vector<CommandTransaction> history_;
    std::size_t cursor_ = 0;
};
}
