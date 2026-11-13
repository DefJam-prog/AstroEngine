#include "astro/logic/VisualLogicGraph.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
int failures = 0;
template<typename T>
void check(const T& condition, const char* message)
{
    if (!static_cast<bool>(condition)) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::logic;
    VisualLogicGraph graph;
    const auto numberA = graph.addNode(NodeKind::NumberConstant);
    const auto numberB = graph.addNode(NodeKind::NumberConstant);
    const auto add = graph.addNode(NodeKind::Add);
    const auto boolean = graph.addNode(NodeKind::BooleanConstant);
    check(numberA && numberB && add && boolean, "create typed nodes");
    if (!numberA || !numberB || !add || !boolean) return EXIT_FAILURE;

    check(!graph.setLiteral(numberA.value(), true), "reject literal with mismatched pin type");
    check(!graph.setLiteral(numberA.value(), std::numeric_limits<double>::infinity()), "reject non-finite number literal");
    check(graph.setLiteral(numberA.value(), 2.0) && graph.setLiteral(numberB.value(), 3.0), "accept finite number literals");
    check(!graph.connect(boolean.value(), add.value(), 0), "reject mismatched connection types");
    check(graph.connect(numberA.value(), add.value(), 0), "connect first number input");
    check(!graph.connect(numberB.value(), add.value(), 0), "reject duplicate input connection");
    check(graph.connect(numberB.value(), add.value(), 1), "connect second number input");
    const auto order = graph.executionOrder();
    check(order && order.value().size() == 4, "validate complete typed graph");
    check(!graph.disconnect(add.value(), 2), "reject out-of-range input index");
    check(graph.disconnect(add.value(), 1), "disconnect a valid input");
    check(!graph.executionOrder(), "reject execution with an unconnected required input");
    check(graph.removeNode(boolean.value()), "remove an existing graph node");
    check(!graph.removeNode(boolean.value()), "reject removing a missing node");

    VisualLogicGraph cyclic("Cycle");
    const auto cycleA = cyclic.addNode(NodeKind::Add);
    const auto cycleB = cyclic.addNode(NodeKind::Add);
    const auto c1 = cyclic.addNode(NodeKind::NumberConstant);
    const auto c2 = cyclic.addNode(NodeKind::NumberConstant);
    check(cycleA && cycleB && c1 && c2, "create cycle validation nodes");
    if (cycleA && cycleB && c1 && c2)
    {
        check(cyclic.connect(c1.value(), cycleA.value(), 1), "configure first node literal input");
        check(cyclic.connect(c2.value(), cycleB.value(), 1), "configure second node literal input");
        check(cyclic.connect(cycleA.value(), cycleB.value(), 0), "create first cycle edge");
        check(!cyclic.connect(cycleB.value(), cycleA.value(), 0), "reject cycle at connection time");
        check(cyclic.connect(c2.value(), cycleA.value(), 0), "complete first arithmetic node after cycle rejection");
        check(cyclic.executionOrder(), "retain valid graph after cycle rejection");
    }

    VisualLogicGraph limited("Node limit");
    for (int i = 0; i < 128; ++i) check(limited.addNode(NodeKind::BooleanConstant), "add node under graph limit");
    check(!limited.addNode(NodeKind::BooleanConstant), "bound graph node count");

    check(std::string_view{nodeKindName(NodeKind::Concatenate)} == "Concatenate", "describe node kinds");
    check(std::string_view{valueTypeName(ValueType::Boolean)} == "Boolean", "describe pin types");

    VisualLogicGraph strings("String concatenation");
    const auto left = strings.addNode(NodeKind::StringConstant);
    const auto right = strings.addNode(NodeKind::StringConstant);
    const auto join = strings.addNode(NodeKind::Concatenate);
    check(left && right && join, "create string nodes");
    if (left && right && join)
    {
        check(strings.setLiteral(left.value(), std::string{"Astro"})
            && strings.setLiteral(right.value(), std::string{"Engine"}), "set typed string literals");
        check(strings.connect(left.value(), join.value(), 0) && strings.connect(right.value(), join.value(), 1),
            "connect string pins");
        VisualLogicDebugger debugger;
        check(debugger.start(strings) && debugger.runToEnd(), "execute string graph");
        const auto* output = debugger.trace(join.value());
        check(output && output->output && std::get<std::string>(*output->output) == "AstroEngine",
            "inspect concatenated output");
    }

    VisualLogicGraph boundedStrings("Bounded string output");
    const auto longLeft = boundedStrings.addNode(NodeKind::StringConstant);
    const auto longRight = boundedStrings.addNode(NodeKind::StringConstant);
    const auto longJoin = boundedStrings.addNode(NodeKind::Concatenate);
    if (longLeft && longRight && longJoin)
    {
        check(boundedStrings.setLiteral(longLeft.value(), std::string(3000, 'a'))
            && boundedStrings.setLiteral(longRight.value(), std::string(2000, 'b')), "set individually bounded strings");
        check(boundedStrings.connect(longLeft.value(), longJoin.value(), 0)
            && boundedStrings.connect(longRight.value(), longJoin.value(), 1), "connect bounded string inputs");
        VisualLogicDebugger debugger;
        check(debugger.start(boundedStrings), "start oversized output graph");
        check(!debugger.runToEnd() && debugger.state() == DebugState::Faulted,
            "reject oversized computed string output");
    }
    check(failures == 0, "all Visual Logic unit checks passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
