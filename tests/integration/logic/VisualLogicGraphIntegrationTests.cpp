#include "astro/logic/VisualLogicGraph.hpp"

#include <cstdlib>
#include <iostream>
#include <variant>

namespace
{
int failures = 0;
template<typename T>
void check(const T& condition, const char* message)
{
    if (!static_cast<bool>(condition)) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

astro::logic::VisualLogicGraph makeScoreGate()
{
    using namespace astro::logic;
    VisualLogicGraph graph("Score gate");
    const auto score = graph.addNode(NodeKind::NumberConstant).value();
    const auto bonus = graph.addNode(NodeKind::NumberConstant).value();
    const auto total = graph.addNode(NodeKind::Add).value();
    const auto threshold = graph.addNode(NodeKind::NumberConstant).value();
    const auto passed = graph.addNode(NodeKind::GreaterThan).value();
    const auto enabled = graph.addNode(NodeKind::BooleanConstant).value();
    const auto result = graph.addNode(NodeKind::And).value();
    (void)graph.setLiteral(score, 4.0);
    (void)graph.setLiteral(bonus, 3.0);
    (void)graph.setLiteral(threshold, 6.0);
    (void)graph.setLiteral(enabled, true);
    (void)graph.connect(score, total, 0);
    (void)graph.connect(bonus, total, 1);
    (void)graph.connect(total, passed, 0);
    (void)graph.connect(threshold, passed, 1);
    (void)graph.connect(passed, result, 0);
    (void)graph.connect(enabled, result, 1);
    return graph;
}
}

int main()
{
    using namespace astro::logic;
    auto graph = makeScoreGate();
    VisualLogicDebugger debugger;
    check(debugger.start(graph), "start debugger from a validated graph snapshot");
    if (debugger.state() != DebugState::Running) return EXIT_FAILURE;
    check(debugger.trace().size() == 7, "create one trace entry per node");
    for (std::size_t i = 0; i < 6; ++i)
    {
        const auto completed = debugger.step();
        check(completed && !completed.value(), "execute one node without skipping ahead");
        check(debugger.trace()[i].state == NodeState::Succeeded, "mark stepped node as succeeded");
        if (i + 1 < debugger.trace().size())
            check(debugger.trace()[i + 1].state == NodeState::Pending, "leave later node pending");
    }
    const auto last = debugger.step();
    check(last && last.value() && debugger.state() == DebugState::Completed, "complete graph after final step");
    const auto& trace = debugger.trace();
    check(trace[2].output && std::get<double>(*trace[2].output) == 7.0, "inspect arithmetic intermediate output");
    check(trace[4].output && std::get<bool>(*trace[4].output), "inspect comparison intermediate output");
    check(trace[6].output && std::get<bool>(*trace[6].output), "inspect final logic output");
    check(debugger.trace(trace[6].node) != nullptr, "look up a trace entry by stable node ID");
    check(debugger.step() && debugger.state() == DebugState::Completed, "completed step does not re-run a node");

    VisualLogicGraph divideGraph;
    const auto numerator = divideGraph.addNode(NodeKind::NumberConstant).value();
    const auto denominator = divideGraph.addNode(NodeKind::NumberConstant).value();
    const auto divide = divideGraph.addNode(NodeKind::Divide).value();
    (void)divideGraph.setLiteral(numerator, 1.0);
    (void)divideGraph.setLiteral(denominator, 0.0);
    (void)divideGraph.connect(numerator, divide, 0);
    (void)divideGraph.connect(denominator, divide, 1);
    check(debugger.start(std::move(divideGraph)), "restart debugger for a second graph");
    const auto divideRun = debugger.runToEnd();
    check(!divideRun, "surface node execution errors");
    check(debugger.state() == DebugState::Faulted && debugger.trace()[2].state == NodeState::Failed,
        "mark failing node and stop evaluation");
    check(debugger.trace()[2].diagnostic == "Division by zero", "retain the node error for inspection");

    VisualLogicGraph incomplete;
    (void)incomplete.addNode(NodeKind::Add);
    check(!debugger.start(std::move(incomplete)) && debugger.state() == DebugState::Faulted,
        "refuse to debug an incomplete graph");
    check(failures == 0, "all Visual Logic integration checks passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
