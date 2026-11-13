#include "astro/prefabs/PrefabLibrary.hpp"

namespace astro::prefabs
{
core::Result<void> registerPrefabCommands(core::CommandRegistry& commands,
    const PrefabLibrary& library, scene::Scene& target)
{
    auto instantiate = commands.registerTransactionalCommand<InstantiatePrefabPayload, scene::EntityId>(
        {"prefab.instantiate", 1},
        [&library, &target](const InstantiatePrefabPayload& payload) {
            scene::Scene probe = target;
            auto result = instantiatePrefab(library, probe, payload.prefab, payload.parent);
            return result ? core::Result<void>::success() : core::Result<void>::failure(result.error());
        },
        [&library, &target](core::EngineContext&, const InstantiatePrefabPayload& payload,
            core::CommandTransaction& transaction) {
            scene::Scene before = target;
            scene::Scene after = target;
            auto result = instantiatePrefab(library, after, payload.prefab, payload.parent);
            if (!result) return core::Result<scene::EntityId>::failure(result.error());
            const auto root = result.value();
            auto recorded = transaction.addStep("instantiate prefab",
                [&target, after] { target = after; return core::Result<void>::success(); },
                [&target, before] { target = before; return core::Result<void>::success(); });
            return recorded ? core::Result<scene::EntityId>::success(root)
                            : core::Result<scene::EntityId>::failure(recorded.error());
        });
    if (!instantiate) return instantiate;

    auto setOverride = commands.registerTransactionalCommand<SetPrefabOverridePayload, void>(
        {"prefab.set_override", 1},
        [&target](const SetPrefabOverridePayload& payload) {
            scene::Scene probe = target;
            return setPrefabOverride(probe, payload.instanceRoot, payload.sourceKey, payload.property, payload.value);
        },
        [&target](core::EngineContext&, const SetPrefabOverridePayload& payload,
            core::CommandTransaction& transaction) {
            scene::Scene before = target;
            scene::Scene after = target;
            auto result = setPrefabOverride(after, payload.instanceRoot, payload.sourceKey, payload.property, payload.value);
            if (!result) return result;
            return transaction.addStep("set prefab override",
                [&target, after] { target = after; return core::Result<void>::success(); },
                [&target, before] { target = before; return core::Result<void>::success(); });
        });
    if (!setOverride) return setOverride;

    return commands.registerTransactionalCommand<UpdatePrefabInstancePayload, void>(
        {"prefab.update_instance", 1},
        [&library, &target](const UpdatePrefabInstancePayload& payload) {
            scene::Scene probe = target;
            return updatePrefabInstance(library, probe, payload.instanceRoot);
        },
        [&library, &target](core::EngineContext&, const UpdatePrefabInstancePayload& payload,
            core::CommandTransaction& transaction) {
            scene::Scene before = target;
            scene::Scene after = target;
            auto result = updatePrefabInstance(library, after, payload.instanceRoot);
            if (!result) return result;
            return transaction.addStep("update prefab instance",
                [&target, after] { target = after; return core::Result<void>::success(); },
                [&target, before] { target = before; return core::Result<void>::success(); });
        });
}
}
