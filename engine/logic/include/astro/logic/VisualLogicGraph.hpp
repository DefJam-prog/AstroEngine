#pragma once

#include "astro/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace astro::logic
{
using NodeId = std::uint32_t;
using Value = std::variant<double, bool, std::string>;

enum class ValueType : std::uint8_t { Number, Boolean, String };
enum class NodeKind : std::uint8_t
{
    NumberConstant,
    BooleanConstant,
    StringConstant,
    Add,
    Subtract,
    Multiply,
    Divide,
    GreaterThan,
    LessThan,
    EqualNumber,
    And,
    Or,
    Not,
    Concatenate
};

enum class NodeState : std::uint8_t { Pending, Succeeded, Failed };
enum class DebugState : std::uint8_t { Idle, Running, Completed, Faulted };

struct Node final
{
    NodeId id = 0;
    NodeKind kind = NodeKind::NumberConstant;
    Value literal = 0.0;
    std::vector<std::optional<NodeId>> inputs;
};

struct NodeTrace final
{
    NodeId node = 0;
    NodeState state = NodeState::Pending;
    std::optional<Value> output;
    std::string diagnostic;
};

[[nodiscard]] const char* nodeKindName(NodeKind kind) noexcept;
[[nodiscard]] const char* valueTypeName(ValueType type) noexcept;
[[nodiscard]] ValueType valueType(const Value& value) noexcept;
[[nodiscard]] std::size_t inputCount(NodeKind kind) noexcept;
[[nodiscard]] std::optional<ValueType> inputType(NodeKind kind, std::size_t index) noexcept;
[[nodiscard]] ValueType outputType(NodeKind kind) noexcept;
[[nodiscard]] std::string formatValue(const Value& value);

class VisualLogicGraph final
{
public:
    explicit VisualLogicGraph(std::string name = "Visual Logic") : name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const Node* find(NodeId id) const noexcept;

    core::Result<NodeId> addNode(NodeKind kind);
    core::Result<void> removeNode(NodeId id);
    core::Result<void> setLiteral(NodeId id, Value value);
    core::Result<void> connect(NodeId source, NodeId target, std::size_t inputIndex);
    core::Result<void> disconnect(NodeId target, std::size_t inputIndex);
    [[nodiscard]] core::Result<std::vector<NodeId>> executionOrder() const;

private:
    [[nodiscard]] bool hasCycle() const;
    std::string name_;
    std::vector<Node> nodes_;
    NodeId nextId_ = 1;
};

// A debugger owns an immutable snapshot of the graph supplied to start().
// One step evaluates one node in deterministic dependency order.
class VisualLogicDebugger final
{
public:
    core::Result<void> start(VisualLogicGraph graph);
    core::Result<bool> step();
    core::Result<void> runToEnd();
    void reset() noexcept;

    [[nodiscard]] DebugState state() const noexcept { return state_; }
    [[nodiscard]] const std::vector<NodeTrace>& trace() const noexcept { return trace_; }
    [[nodiscard]] const NodeTrace* trace(NodeId id) const noexcept;

private:
    [[nodiscard]] core::Result<Value> evaluate(const Node& node) const;
    VisualLogicGraph graph_;
    std::vector<NodeId> order_;
    std::vector<NodeTrace> trace_;
    std::map<NodeId, Value> values_;
    std::size_t nextStep_ = 0;
    DebugState state_ = DebugState::Idle;
};
}
