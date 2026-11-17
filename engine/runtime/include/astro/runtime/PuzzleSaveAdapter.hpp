#pragma once

#include "astro/runtime/PuzzleGame.hpp"
#include "astro/runtime/SaveStore.hpp"

#include <string>
#include <string_view>

namespace astro::runtime
{
namespace PuzzleSaveField
{
inline constexpr std::string_view snapshot = "puzzle.snapshot";
inline constexpr std::string_view levelId = "puzzle.level.id";
}

struct PuzzleSnapshotLoadResult final
{
    PuzzleGame puzzle;
    SaveLoadResult save;
    SaveCustomState additionalCustomState;
};

// Adds one explicit string field to a profile. Existing fields are left unchanged.
core::Result<void> selectPuzzleSnapshotField(SaveProfile& profile,
    std::string key = std::string(PuzzleSaveField::snapshot));
core::Result<void> selectPuzzleLevelIdentityField(SaveProfile& profile,
    std::string key = std::string(PuzzleSaveField::levelId));

// The caller supplies every other custom value selected by the profile.
core::Result<SaveWriteResult> savePuzzleSnapshot(const SaveStore& store, std::size_t slot,
    const scene::Scene& scene, const SaveProfile& profile, std::string_view gameVersion,
    const PuzzleGame& puzzle, const SaveCustomState& additionalCustomState = {},
    std::string_view snapshotKey = PuzzleSaveField::snapshot);

// Scene changes are published only after SaveStore validation, payload decoding and
// PuzzleGame restoration have all succeeded.
core::Result<PuzzleSnapshotLoadResult> loadPuzzleSnapshot(const SaveStore& store, std::size_t slot,
    scene::Scene& scene, const SaveProfile& profile, std::string_view gameVersion,
    std::string_view snapshotKey = PuzzleSaveField::snapshot);

// These helpers add and verify a stable authored-level ID. A mismatched level fails
// before publishing Scene or Puzzle state; callers still choose the user-data directory.
core::Result<SaveWriteResult> savePuzzleLevelSnapshot(const SaveStore& store, std::size_t slot,
    const scene::Scene& scene, const SaveProfile& profile, std::string_view gameVersion,
    const PuzzleGame& puzzle, std::string_view levelId,
    const SaveCustomState& additionalCustomState = {});
core::Result<PuzzleSnapshotLoadResult> loadPuzzleLevelSnapshot(const SaveStore& store, std::size_t slot,
    scene::Scene& scene, const SaveProfile& profile, std::string_view gameVersion,
    std::string_view expectedLevelId);
}
