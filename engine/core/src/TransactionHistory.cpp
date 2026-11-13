#include "astro/core/TransactionHistory.hpp"

#include <exception>

namespace astro::core
{
Result<void> CommandTransaction::addStep(std::string label, Action apply, Action undo)
{
    if (label.empty() || !apply || !undo)
        return Result<void>::failure({ErrorCode::InvalidArgument, "Transaction step requires a label, apply and undo actions"});
    steps_.push_back({std::move(label), std::move(apply), std::move(undo)});
    return Result<void>::success();
}

Result<void> TransactionHistory::invoke(const CommandTransaction::Action& action, const std::string& transactionLabel,
    const std::string& stepLabel, const char* direction)
{
    try
    {
        auto result = action();
        if (!result)
            return Result<void>::failure({result.error().code,
                "Transaction '" + transactionLabel + "' step '" + stepLabel + "' " + direction + " failed: " + result.error().message});
        return Result<void>::success();
    }
    catch (const std::exception& error)
    {
        return Result<void>::failure({ErrorCode::InternalError,
            "Transaction '" + transactionLabel + "' step '" + stepLabel + "' " + direction + " threw: " + error.what()});
    }
    catch (...)
    {
        return Result<void>::failure({ErrorCode::InternalError,
            "Transaction '" + transactionLabel + "' step '" + stepLabel + "' " + direction + " threw an unknown exception"});
    }
}

Error TransactionHistory::appendRecoveryErrors(Error primary, const std::vector<Error>& recoveryErrors,
    const std::string& transactionLabel)
{
    for (const auto& error : recoveryErrors)
        primary.message += "\nRecovery of transaction '" + transactionLabel + "' also failed: " + error.message;
    return primary;
}

Result<void> TransactionHistory::commit(CommandTransaction transaction)
{
    if (transaction.label_.empty())
        return Result<void>::failure({ErrorCode::InvalidArgument, "Transaction label cannot be empty"});
    if (transaction.steps_.empty()) return Result<void>::success();
    try
    {
        history_.reserve(cursor_ + 1);
    }
    catch (const std::exception& error)
    {
        return Result<void>::failure({ErrorCode::InternalError, std::string("Cannot reserve transaction history: ") + error.what()});
    }

    std::size_t appliedCount = 0;
    for (const auto& step : transaction.steps_)
    {
        auto result = invoke(step.apply, transaction.label_, step.label, "apply");
        if (!result)
        {
            std::vector<Error> recoveryErrors;
            while (appliedCount > 0)
            {
                --appliedCount;
                const auto& applied = transaction.steps_[appliedCount];
                auto rollback = invoke(applied.undo, transaction.label_, applied.label, "rollback");
                if (!rollback) recoveryErrors.push_back(rollback.error());
            }
            if (!recoveryErrors.empty()) clear();
            return Result<void>::failure(appendRecoveryErrors(result.error(), recoveryErrors, transaction.label_));
        }
        ++appliedCount;
    }

    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
    history_.push_back(std::move(transaction));
    cursor_ = history_.size();
    return Result<void>::success();
}

Result<void> TransactionHistory::undo()
{
    if (!canUndo()) return Result<void>::failure({ErrorCode::InvalidState, "Undo history is empty"});
    const auto transaction = history_[cursor_ - 1];
    std::vector<std::size_t> undoneSteps;
    for (std::size_t index = transaction.steps_.size(); index > 0; --index)
    {
        const auto stepIndex = index - 1;
        auto result = invoke(transaction.steps_[stepIndex].undo, transaction.label_, transaction.steps_[stepIndex].label, "undo");
        if (!result)
        {
            std::vector<Error> recoveryErrors;
            for (auto redo = undoneSteps.rbegin(); redo != undoneSteps.rend(); ++redo)
            {
                const auto& step = transaction.steps_[*redo];
                auto recovery = invoke(step.apply, transaction.label_, step.label, "undo recovery");
                if (!recovery) recoveryErrors.push_back(recovery.error());
            }
            if (!recoveryErrors.empty()) clear();
            return Result<void>::failure(appendRecoveryErrors(result.error(), recoveryErrors, transaction.label_));
        }
        undoneSteps.push_back(stepIndex);
    }
    --cursor_;
    return Result<void>::success();
}

Result<void> TransactionHistory::redo()
{
    if (!canRedo()) return Result<void>::failure({ErrorCode::InvalidState, "Redo history is empty"});
    const auto transaction = history_[cursor_];
    std::size_t appliedCount = 0;
    for (const auto& step : transaction.steps_)
    {
        auto result = invoke(step.apply, transaction.label_, step.label, "redo");
        if (!result)
        {
            std::vector<Error> recoveryErrors;
            while (appliedCount > 0)
            {
                --appliedCount;
                const auto& applied = transaction.steps_[appliedCount];
                auto recovery = invoke(applied.undo, transaction.label_, applied.label, "redo recovery");
                if (!recovery) recoveryErrors.push_back(recovery.error());
            }
            if (!recoveryErrors.empty()) clear();
            return Result<void>::failure(appendRecoveryErrors(result.error(), recoveryErrors, transaction.label_));
        }
        ++appliedCount;
    }
    ++cursor_;
    return Result<void>::success();
}

void TransactionHistory::clear() noexcept
{
    history_.clear();
    cursor_ = 0;
}

std::optional<std::pair<std::string, std::uint32_t>> TransactionHistory::nextUndoOrigin() const
{
    if (!canUndo() || history_[cursor_ - 1].commandId_.empty()) return std::nullopt;
    return std::pair{history_[cursor_ - 1].commandId_, history_[cursor_ - 1].commandVersion_};
}

std::optional<std::pair<std::string, std::uint32_t>> TransactionHistory::nextRedoOrigin() const
{
    if (!canRedo() || history_[cursor_].commandId_.empty()) return std::nullopt;
    return std::pair{history_[cursor_].commandId_, history_[cursor_].commandVersion_};
}
}
