# Visual Logic — 06.1 prototype

## What is available

The optional Visual Logic editor window edits a typed dataflow graph. Open `View → Visual Logic`. Add Number, Boolean, and String constants, then add operators from the Math, Compare, Logic, and Text menus. Each operator input offers only nodes with a matching output type. A connection can be removed from its input menu; a node can be removed with its `Remove node` button.

Use `Run` to evaluate the whole graph, `Step` to evaluate one node at a time, and `Reset debug` to clear the trace. Successful node outputs and failures are displayed under each node. An incomplete graph, a cycle, division by zero, a non-finite numeric result, or an oversized string output is reported without continuing evaluation past the error.

The editor starts with a runnable `Score gate example` graph:

```text
(4 + 3) > 6 AND true  →  true
```

The current catalog includes number/boolean/string constants; add, subtract, multiply, divide; greater-than, less-than and numeric equality; AND, OR, NOT; and string concatenation. Graph evaluation is deterministic and uses stable node IDs to break ties between ready nodes.

## C++ API example

```cpp
astro::logic::VisualLogicGraph graph("Score check");
const auto score = graph.addNode(astro::logic::NodeKind::NumberConstant);
const auto threshold = graph.addNode(astro::logic::NodeKind::NumberConstant);
const auto check = graph.addNode(astro::logic::NodeKind::GreaterThan);

graph.setLiteral(score.value(), 12.0);
graph.setLiteral(threshold.value(), 10.0);
graph.connect(score.value(), check.value(), 0);
graph.connect(threshold.value(), check.value(), 1);

astro::logic::VisualLogicDebugger debugger;
debugger.start(graph); // takes a snapshot
debugger.step();       // evaluates one ready node
debugger.runToEnd();   // continue from the current step
```

Callers should check every `Result` before using its value. The debugger owns a graph snapshot; edits to the original graph do not affect an active run. Query `debugger.trace()` for each node's Pending/Succeeded/Failed state, output, or diagnostic.

## Limits of 06.1

- Graphs have a 128-node limit. String literals and generated text outputs are limited to 4096 bytes.
- The editor is a vertical node list. Connections are selected from typed menus rather than drawn as drag-and-drop wires.
- Graphs exist only in editor memory and are lost when the editor closes. No project graph file format, save/load, undo history, or collaboration is implemented.
- V1 is a pure dataflow evaluator. It has no execution-flow branches/events, Scene mutation, gameplay command nodes, or runtime binding. Evaluating a graph does not change the project scene or gameplay state.
- Debug execution is synchronous and single-threaded. There are no breakpoints or performance profiler integration.

`astro_visual_logic` is a C++20 target without SDL, bgfx, or ImGui dependencies. Build it in a non-editor configuration with `-DASTRO_BUILD_VISUAL_LOGIC=ON`. Editor UI requires the existing optional Windows editor stack.
