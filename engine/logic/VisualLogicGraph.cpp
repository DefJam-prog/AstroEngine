#include "astro/logic/VisualLogicGraph.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>

namespace astro::logic
{
namespace
{
core::Error graphError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

Value defaultLiteral(const NodeKind kind)
{
    switch (kind)
    {
    case NodeKind::BooleanConstant: return false;
    case NodeKind::StringConstant: return std::string{};
    default: return 0.0;
    }
}

bool isConstant(const NodeKind kind) noexcept
{
    return kind == NodeKind::NumberConstant || kind == NodeKind::BooleanConstant
        || kind == NodeKind::StringConstant;
}

bool validNodeKind(const NodeKind kind) noexcept
{
    return kind >= NodeKind::NumberConstant && kind <= NodeKind::Concatenate;
}

bool isCompatibleLiteral(const NodeKind kind, const Value& value) noexcept
{
    if (kind == NodeKind::NumberConstant) return std::holds_alternative<double>(value);
    if (kind == NodeKind::BooleanConstant) return std::holds_alternative<bool>(value);
    if (kind == NodeKind::StringConstant) return std::holds_alternative<std::string>(value);
    return false;
}

bool finiteNumber(const Value& value) noexcept
{
    const auto* number = std::get_if<double>(&value);
    return number && std::isfinite(*number);
}
}

const char* nodeKindName(const NodeKind kind) noexcept
{
    switch (kind)
    {
    case NodeKind::NumberConstant: return "Number";
    case NodeKind::BooleanConstant: return "Boolean";
    case NodeKind::StringConstant: return "String";
    case NodeKind::Add: return "Add";
    case NodeKind::Subtract: return "Subtract";
    case NodeKind::Multiply: return "Multiply";
    case NodeKind::Divide: return "Divide";
    case NodeKind::GreaterThan: return "Greater Than";
    case NodeKind::LessThan: return "Less Than";
    case NodeKind::EqualNumber: return "Equal Number";
    case NodeKind::And: return "And";
    case NodeKind::Or: return "Or";
    case NodeKind::Not: return "Not";
    case NodeKind::Concatenate: return "Concatenate";
    }
    return "Unknown";
}

const char* valueTypeName(const ValueType type) noexcept
{
    switch (type)
    {
    case ValueType::Number: return "Number";
    case ValueType::Boolean: return "Boolean";
    case ValueType::String: return "String";
    }
    return "Unknown";
}

ValueType valueType(const Value& value) noexcept
{
    if (std::holds_alternative<double>(value)) return ValueType::Number;
    if (std::holds_alternative<bool>(value)) return ValueType::Boolean;
    return ValueType::String;
}

std::size_t inputCount(const NodeKind kind) noexcept
{
    switch (kind)
    {
    case NodeKind::Add:
    case NodeKind::Subtract:
    case NodeKind::Multiply:
    case NodeKind::Divide:
    case NodeKind::GreaterThan:
    case NodeKind::LessThan:
    case NodeKind::EqualNumber:
    case NodeKind::And:
    case NodeKind::Or:
    case NodeKind::Concatenate: return 2;
    case NodeKind::Not: return 1;
    default: return 0;
    }
}

std::optional<ValueType> inputType(const NodeKind kind, const std::size_t index) noexcept
{
    if (index >= inputCount(kind)) return std::nullopt;
    switch (kind)
    {
    case NodeKind::And:
    case NodeKind::Or:
    case NodeKind::Not: return ValueType::Boolean;
    case NodeKind::Concatenate: return ValueType::String;
    default: return ValueType::Number;
    }
}

ValueType outputType(const NodeKind kind) noexcept
{
    switch (kind)
    {
    case NodeKind::BooleanConstant:
    case NodeKind::GreaterThan:
    case NodeKind::LessThan:
    case NodeKind::EqualNumber:
    case NodeKind::And:
    case NodeKind::Or:
    case NodeKind::Not: return ValueType::Boolean;
    case NodeKind::StringConstant:
    case NodeKind::Concatenate: return ValueType::String;
    default: return ValueType::Number;
    }
}

std::string formatValue(const Value& value)
{
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::string>) return '"' + item + '"';
        else
        {
            std::ostringstream stream;
            stream << std::setprecision(std::numeric_limits<double>::max_digits10) << item;
            return stream.str();
        }
    }, value);
}

const Node* VisualLogicGraph::find(const NodeId id) const noexcept
{
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& node) { return node.id == id; });
    return found == nodes_.end() ? nullptr : &*found;
}

core::Result<NodeId> VisualLogicGraph::addNode(const NodeKind kind)
{
    if (!validNodeKind(kind))
        return core::Result<NodeId>::failure(graphError(core::ErrorCode::InvalidArgument, "Unknown Visual Logic node kind"));
    if (nodes_.size() >= 128)
        return core::Result<NodeId>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic graph exceeds 128 nodes"));
    if (nextId_ == 0)
        return core::Result<NodeId>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic node ID space is exhausted"));
    const auto id = nextId_++;
    nodes_.push_back({id, kind, defaultLiteral(kind), std::vector<std::optional<NodeId>>(inputCount(kind))});
    return core::Result<NodeId>::success(id);
}

core::Result<void> VisualLogicGraph::removeNode(const NodeId id)
{
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& node) { return node.id == id; });
    if (found == nodes_.end())
        return core::Result<void>::failure(graphError(core::ErrorCode::NotFound, "Visual Logic node was not found"));
    for (auto& node : nodes_)
        for (auto& connection : node.inputs)
            if (connection == id) connection.reset();
    nodes_.erase(found);
    return core::Result<void>::success();
}

core::Result<void> VisualLogicGraph::setLiteral(const NodeId id, Value value)
{
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& node) { return node.id == id; });
    auto* node = found == nodes_.end() ? nullptr : &*found;
    if (!node || !isConstant(node->kind) || !isCompatibleLiteral(node->kind, value))
        return core::Result<void>::failure(graphError(core::ErrorCode::TypeMismatch,
            "Literal value type does not match the selected Visual Logic node"));
    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        return core::Result<void>::failure(graphError(core::ErrorCode::ValidationError, "Number literal must be finite"));
    if (const auto* text = std::get_if<std::string>(&value);
        text && (text->size() > 4096 || text->find('\0') != std::string::npos))
        return core::Result<void>::failure(graphError(core::ErrorCode::ValidationError, "String literal exceeds limits or contains NUL"));
    node->literal = std::move(value);
    return core::Result<void>::success();
}

core::Result<void> VisualLogicGraph::connect(const NodeId source, const NodeId target, const std::size_t inputIndex)
{
    const auto* sourceNode = find(source);
    const auto targetFound = std::find_if(nodes_.begin(), nodes_.end(), [target](const Node& node) { return node.id == target; });
    auto* targetNode = targetFound == nodes_.end() ? nullptr : &*targetFound;
    if (!sourceNode || !targetNode)
        return core::Result<void>::failure(graphError(core::ErrorCode::NotFound, "Visual Logic connection references a missing node"));
    const auto expected = inputType(targetNode->kind, inputIndex);
    if (!expected)
        return core::Result<void>::failure(graphError(core::ErrorCode::InvalidArgument, "Visual Logic input index is out of range"));
    if (outputType(sourceNode->kind) != *expected)
        return core::Result<void>::failure(graphError(core::ErrorCode::TypeMismatch, "Visual Logic connection types do not match"));
    if (targetNode->inputs[inputIndex])
        return core::Result<void>::failure(graphError(core::ErrorCode::InvalidArgument, "Visual Logic input is already connected"));

    targetNode->inputs[inputIndex] = source;
    if (hasCycle())
    {
        targetNode->inputs[inputIndex].reset();
        return core::Result<void>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic connections must not contain cycles"));
    }
    return core::Result<void>::success();
}

core::Result<void> VisualLogicGraph::disconnect(const NodeId target, const std::size_t inputIndex)
{
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [target](const Node& node) { return node.id == target; });
    auto* node = found == nodes_.end() ? nullptr : &*found;
    if (!node) return core::Result<void>::failure(graphError(core::ErrorCode::NotFound, "Visual Logic node was not found"));
    if (inputIndex >= node->inputs.size())
        return core::Result<void>::failure(graphError(core::ErrorCode::InvalidArgument, "Visual Logic input index is out of range"));
    node->inputs[inputIndex].reset();
    return core::Result<void>::success();
}

bool VisualLogicGraph::hasCycle() const
{
    std::map<NodeId, std::uint8_t> marks;
    const auto visit = [&](const auto& self, const NodeId id) -> bool {
        auto& mark = marks[id];
        if (mark == 1) return true;
        if (mark == 2) return false;
        mark = 1;
        for (const auto& candidate : nodes_)
            if (std::find(candidate.inputs.begin(), candidate.inputs.end(), std::optional<NodeId>{id}) != candidate.inputs.end()
                && self(self, candidate.id)) return true;
        mark = 2;
        return false;
    };
    for (const auto& node : nodes_) if (visit(visit, node.id)) return true;
    return false;
}

core::Result<std::vector<NodeId>> VisualLogicGraph::executionOrder() const
{
    if (nodes_.empty())
        return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic graph has no nodes"));
    std::map<NodeId, std::size_t> indegree;
    std::map<NodeId, std::vector<NodeId>> dependents;
    for (const auto& node : nodes_)
    {
        if (node.inputs.size() != inputCount(node.kind))
            return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic node input layout is invalid"));
        if (isConstant(node.kind) && !isCompatibleLiteral(node.kind, node.literal))
            return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::TypeMismatch, "Visual Logic node literal type is invalid"));
        if (node.kind == NodeKind::NumberConstant && !finiteNumber(node.literal))
            return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic number literal must be finite"));
        if (node.kind == NodeKind::StringConstant)
        {
            const auto& value = std::get<std::string>(node.literal);
            if (value.size() > 4096 || value.find('\0') != std::string::npos)
                return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic string literal exceeds limits or contains NUL"));
        }
        indegree.emplace(node.id, 0);
    }
    for (const auto& node : nodes_)
    {
        for (std::size_t index = 0; index < node.inputs.size(); ++index)
        {
            if (!node.inputs[index])
                return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::ValidationError,
                    std::string("Visual Logic input is unconnected on node ") + std::to_string(node.id)));
            const auto* source = find(*node.inputs[index]);
            const auto expected = inputType(node.kind, index);
            if (!source || !expected)
                return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::NotFound, "Visual Logic connection references a missing node"));
            if (outputType(source->kind) != *expected)
                return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::TypeMismatch, "Visual Logic connection types do not match"));
            ++indegree[node.id];
            dependents[source->id].push_back(node.id);
        }
    }
    std::set<NodeId> ready;
    for (const auto& [id, count] : indegree) if (count == 0) ready.insert(id);
    std::vector<NodeId> order;
    order.reserve(nodes_.size());
    while (!ready.empty())
    {
        const auto id = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(id);
        for (const auto dependent : dependents[id])
            if (--indegree[dependent] == 0) ready.insert(dependent);
    }
    if (order.size() != nodes_.size())
        return core::Result<std::vector<NodeId>>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic graph contains a cycle"));
    return core::Result<std::vector<NodeId>>::success(std::move(order));
}

core::Result<void> VisualLogicDebugger::start(VisualLogicGraph graph)
{
    auto order = graph.executionOrder();
    if (!order)
    {
        reset();
        state_ = DebugState::Faulted;
        return core::Result<void>::failure(order.error());
    }
    graph_ = std::move(graph);
    order_ = std::move(order.value());
    trace_.clear();
    values_.clear();
    trace_.reserve(order_.size());
    for (const auto id : order_) trace_.push_back({id, NodeState::Pending, std::nullopt, {}});
    nextStep_ = 0;
    state_ = DebugState::Running;
    return core::Result<void>::success();
}

core::Result<Value> VisualLogicDebugger::evaluate(const Node& node) const
{
    if (isConstant(node.kind)) return core::Result<Value>::success(node.literal);
    std::vector<Value> inputs;
    inputs.reserve(node.inputs.size());
    for (const auto& source : node.inputs)
    {
        if (!source || !values_.contains(*source))
            return core::Result<Value>::failure(graphError(core::ErrorCode::ValidationError, "Visual Logic input value is not available"));
        inputs.push_back(values_.at(*source));
    }
    const auto number = [&inputs](const std::size_t index) { return std::get<double>(inputs[index]); };
    const auto boolean = [&inputs](const std::size_t index) { return std::get<bool>(inputs[index]); };
    switch (node.kind)
    {
    case NodeKind::Add: return core::Result<Value>::success(number(0) + number(1));
    case NodeKind::Subtract: return core::Result<Value>::success(number(0) - number(1));
    case NodeKind::Multiply: return core::Result<Value>::success(number(0) * number(1));
    case NodeKind::Divide:
        if (number(1) == 0.0) return core::Result<Value>::failure(graphError(core::ErrorCode::ValidationError, "Division by zero"));
        return core::Result<Value>::success(number(0) / number(1));
    case NodeKind::GreaterThan: return core::Result<Value>::success(number(0) > number(1));
    case NodeKind::LessThan: return core::Result<Value>::success(number(0) < number(1));
    case NodeKind::EqualNumber: return core::Result<Value>::success(number(0) == number(1));
    case NodeKind::And: return core::Result<Value>::success(boolean(0) && boolean(1));
    case NodeKind::Or: return core::Result<Value>::success(boolean(0) || boolean(1));
    case NodeKind::Not: return core::Result<Value>::success(!boolean(0));
    case NodeKind::Concatenate:
    {
        const auto& left = std::get<std::string>(inputs[0]);
        const auto& right = std::get<std::string>(inputs[1]);
        if (left.size() > 4096 || right.size() > 4096 - left.size())
            return core::Result<Value>::failure(graphError(core::ErrorCode::ValidationError, "Concatenated string exceeds 4096 bytes"));
        return core::Result<Value>::success(left + right);
    }
    default: return core::Result<Value>::failure(graphError(core::ErrorCode::InvalidArgument, "Unknown Visual Logic node kind"));
    }
}

core::Result<bool> VisualLogicDebugger::step()
{
    if (state_ == DebugState::Idle || state_ == DebugState::Faulted)
        return core::Result<bool>::failure(graphError(core::ErrorCode::InvalidArgument, "Visual Logic debugger is not running"));
    if (state_ == DebugState::Completed) return core::Result<bool>::success(true);
    const auto id = order_[nextStep_];
    const auto* node = graph_.find(id);
    auto result = node ? evaluate(*node)
        : core::Result<Value>::failure(graphError(core::ErrorCode::NotFound, "Visual Logic node disappeared during execution"));
    auto& entry = trace_[nextStep_];
    if (!result)
    {
        entry.state = NodeState::Failed;
        entry.diagnostic = result.error().message;
        state_ = DebugState::Faulted;
        return core::Result<bool>::failure(result.error());
    }
    if (const auto* value = std::get_if<double>(&result.value()); value && !std::isfinite(*value))
    {
        entry.state = NodeState::Failed;
        entry.diagnostic = "Numeric output is not finite";
        state_ = DebugState::Faulted;
        return core::Result<bool>::failure(graphError(core::ErrorCode::ValidationError, entry.diagnostic));
    }
    if (const auto* text = std::get_if<std::string>(&result.value()); text && text->size() > 4096)
    {
        entry.state = NodeState::Failed;
        entry.diagnostic = "String output exceeds 4096 bytes";
        state_ = DebugState::Faulted;
        return core::Result<bool>::failure(graphError(core::ErrorCode::ValidationError, entry.diagnostic));
    }
    entry.state = NodeState::Succeeded;
    entry.output = result.value();
    values_[id] = result.value();
    ++nextStep_;
    if (nextStep_ == order_.size()) state_ = DebugState::Completed;
    return core::Result<bool>::success(state_ == DebugState::Completed);
}

core::Result<void> VisualLogicDebugger::runToEnd()
{
    if (state_ == DebugState::Idle || state_ == DebugState::Faulted)
        return core::Result<void>::failure(graphError(core::ErrorCode::InvalidArgument, "Visual Logic debugger is not running"));
    while (state_ == DebugState::Running)
    {
        const auto result = step();
        if (!result) return core::Result<void>::failure(result.error());
    }
    return core::Result<void>::success();
}

void VisualLogicDebugger::reset() noexcept
{
    graph_ = VisualLogicGraph{};
    order_.clear();
    trace_.clear();
    values_.clear();
    nextStep_ = 0;
    state_ = DebugState::Idle;
}

const NodeTrace* VisualLogicDebugger::trace(const NodeId id) const noexcept
{
    const auto found = std::find_if(trace_.begin(), trace_.end(), [id](const NodeTrace& item) { return item.node == id; });
    return found == trace_.end() ? nullptr : &*found;
}
}
