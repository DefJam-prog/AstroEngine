#include "astro/scene/Scene.hpp"

namespace astro::scene
{
core::Result<void> registerSceneCommands(core::CommandRegistry& commands, Scene& scene)
{
    return commands.registerTransactionalCommand<SetScenePropertyPayload, void>(
        {"entity.set_property", 1},
        [&scene](const SetScenePropertyPayload& payload) {
            return scene.validateProperty(payload.entity, payload.property, payload.value);
        },
        [&scene](core::EngineContext&, const SetScenePropertyPayload& payload, core::CommandTransaction& transaction) {
            auto previous = scene.getProperty(payload.entity, payload.property);
            if (!previous) return core::Result<void>::failure(previous.error());
            const auto property = payload.property;
            const auto before = std::move(previous).value();
            const auto after = payload.value;
            return transaction.addStep("set entity property",
                [&scene, entity = payload.entity, property, after] {
                    return scene.setProperty(entity, property, after);
                },
                [&scene, entity = payload.entity, property, before] {
                    return scene.setProperty(entity, property, before);
                });
        });
}
}
