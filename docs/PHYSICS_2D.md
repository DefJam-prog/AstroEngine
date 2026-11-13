# Runtime 2D Physics 05.1

`astro_runtime_physics2d` is a small dependency-free runtime module. It binds one `Body2D` and `BoxCollider2D` to each selected entity in one scene. Call `step` from the `RuntimeSession` fixed-step callback and pass the runtime Scene copy:

```cpp
astro::runtime::PhysicsWorld2D physics;
astro::core::EventBus events;
physics.setGravity({0.0, -9.81});

// playerId and floorId refer to entities already present in the play Scene.
physics.addBody(playScene, floorId,
    {astro::runtime::BodyType2D::Static, {}, 0.0}, {{4.0, 0.5}, false, true});
physics.addBody(playScene, playerId,
    {astro::runtime::BodyType2D::Dynamic, {}, 1.0}, {{0.4, 0.8}, false, true});

events.subscribe<astro::runtime::CollisionEvent>([](const auto& event) {
    // Enter, Stay, Exit; first/second IDs are sorted and normal points first -> second.
});
physics.step(playScene, events, fixedDeltaSeconds);
```

Dynamic bodies integrate velocity and gravity. AABB overlap is checked using Transform XY position and absolute XY scale. Solid overlaps receive positional correction along the smaller penetration axis; dynamic bodies stop along that collision axis. If either collider is a trigger, an event is sent without physical correction. Contact events are ordered by entity ID and sequence number and describe `Enter`, `Stay`, or `Exit`.

## Limits

- Bindings are code-created runtime state, not Scene/prefab data or editor UI. Bind one world to one SceneId and register its bodies on the RuntimeSession Scene copy.
- Boxes are axis-aligned; Transform rotation and Z are ignored. Only static and dynamic bodies are supported.
- Detection is discrete and pairwise, with a 2,048-body cap. Fast bodies can tunnel through thin colliders; many-body scenes can be slow.
- Resolution is basic positional correction with no mass, restitution, friction, joints, continuous collision detection, or angular motion. This is not a general-purpose physics engine.
- The caller must invoke `step` with the fixed timestep and handle event-observer errors. Physics state changes are not rolled back if an observer throws.
