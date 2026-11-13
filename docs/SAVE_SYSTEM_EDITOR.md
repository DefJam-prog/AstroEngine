# Save System Editor (06.4)

The optional Windows Editor opens this panel through **View → Save System**.
It is an authoring prototype layered over the existing selective runtime
`SaveProfile`, `SaveStore`, and `CheckpointSystem` APIs.

## Profiles and groups

Create an empty profile and add fields to the selected Hierarchy entity. A
group is an explicit list of entity IDs; add/remove the selected entity. A
profile can select a property directly for one entity or select that property
for every member of a group. The panel displays the flattened effective field
list. It never selects the whole scene implicitly.

Supported properties are `entity.name`, Transform position/rotation/scale
components, and `sprite_renderer.visible`, subject to the corresponding
component existing on each entity. Compilation fails with a diagnostic if a
group rule references an entity without the selected property. Unsupported
gameplay fields such as health, inventory, fuel, and puzzle progress are not
offered.

## Checkpoints

Select an entity with Transform to add or edit a checkpoint: XY area half-size,
respawn XYZ, activation mode, repeat mode, enabled state, profile, and slot.
The panel validates the definition through `CheckpointSystem`. Its XY map is
a preview from existing Transform coordinates; it does not reposition scene
entities or render a gameplay trigger. Conditions, audio/visual effects, and
checkpoint serialization are not part of this prototype.

**Test checkpoint activation and restore** runs the actual checkpoint and save
runtime against a copied Scene and a newly created unique directory under the
OS temporary directory. It removes only that directory after the test. The
profile-backed preview requires `entity.name` to be explicitly selected for
the first non-checkpoint entity used as the test actor. A position-only
checkpoint tests activation and respawn placement.

## Persistence and limitations

Groups, profile drafts, and checkpoint definitions currently live only in
editor memory and are lost when the editor closes. They are not silently
written into scene files or player save folders. Project settings serialization,
undo/redo for this panel, profile templates, tags/filters/exclusions,
profiles as runtime assets, and richer respawn rules remain future work.

No external dependency was added. See [SAVE_SYSTEM.md](SAVE_SYSTEM.md) for the
runtime save format and [report 06.4](reports/06.4.md) for verified results.
