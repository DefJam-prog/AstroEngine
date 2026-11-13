#include "astro/save_editor/SaveSystemDocument.hpp"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL: " #expression "\n"; ++failures; } } while (false)
}

int main()
{
    using namespace astro;
    scene::Scene scene(501, "Save editor test");
    const auto first = scene.createEntity("First");
    const auto second = scene.createEntity("Second");
    CHECK(first && second);
    save_editor::SaveSystemDocument document;
    CHECK(document.createGroup("players"));
    CHECK(document.setGroupMember("players", first.value(), true));
    CHECK(document.setGroupMember("players", second.value(), true));
    CHECK(document.createProfile("progress", 2));
    CHECK(document.setGroupField("progress", "players", std::string(runtime::SaveProperty::entityName), true));
    CHECK(document.setField("progress", first.value(), std::string(runtime::SaveProperty::positionX), true));
    CHECK(document.setField("progress", first.value(), std::string(runtime::SaveProperty::entityName), true));
    const auto compiled = document.compileProfiles(scene);
    CHECK(compiled);
    if (compiled)
    {
        const auto& fields = compiled.value().at("progress").fields();
        CHECK(fields.at(first.value()).size() == 2);
        CHECK(fields.at(second.value()).size() == 1);
        CHECK(fields.at(second.value()).front() == runtime::SaveProperty::entityName);
    }
    CHECK(!document.setField("progress", first.value(), "unsupported.property", true));
    CHECK(!document.setGroupMember("missing", first.value(), true));
    CHECK(document.setField("progress", first.value(), std::string(runtime::SaveProperty::spriteVisible), true));
    CHECK(!document.compileProfiles(scene));
    CHECK(document.setField("progress", first.value(), std::string(runtime::SaveProperty::spriteVisible), false));

    runtime::CheckpointDefinition checkpoint;
    checkpoint.entity = first.value();
    checkpoint.mode = runtime::CheckpointMode::SaveSelectedProfile;
    checkpoint.saveProfileId = "progress";
    CHECK(document.setCheckpoint(checkpoint));
    if (compiled)
    {
        const auto validCheckpoints = document.validateCheckpoints(scene, compiled.value());
        CHECK(validCheckpoints && validCheckpoints.value().size() == 1);
    }
    CHECK(document.removeCheckpoint(first.value()));
    CHECK(document.checkpoints().empty());
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
