#include "astro/scene/Scene.hpp"

namespace astro::scene
{
core::Result<void> registerSceneCommands(core::CommandRegistry& commands, Scene& scene)
{
    auto property = commands.registerTransactionalCommand<SetScenePropertyPayload, void>(
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
    if (!property) return property;
    return commands.registerTransactionalCommand<SetSpriteRendererPayload, void>(
        {"entity.set_sprite_renderer", 1},
        [&scene](const SetSpriteRendererPayload& payload) {
            if (!scene.find(payload.entity))
                return core::Result<void>::failure({core::ErrorCode::NotFound, "Entity does not exist"});
            if (payload.texture.value == 0)
                return core::Result<void>::failure({core::ErrorCode::ValidationError, "Texture AssetId must not be zero"});
            return core::Result<void>::success();
        },
        [&scene](core::EngineContext&, const SetSpriteRendererPayload& payload, core::CommandTransaction& transaction) {
            const auto* existing = scene.spriteRenderer(payload.entity);
            const auto before = existing ? std::optional<SpriteRendererComponent>{*existing} : std::nullopt;
            return transaction.addStep("set SpriteRenderer",
                [&scene, payload] { return scene.setSpriteRenderer(payload.entity, payload.texture, payload.visible); },
                [&scene, entity = payload.entity, before] {
                    if (before) return scene.setSpriteRenderer(entity, before->texture, before->visible);
                    return scene.removeSpriteRenderer(entity);
                });
        });
}
}
