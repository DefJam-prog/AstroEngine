#pragma once

#include "astro/runtime/PuzzleCampaignSession.hpp"
#include "astro/runtime/PuzzleSaveAdapter.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace astro::runtime
{
namespace PuzzleCampaignSaveField
{
inline constexpr std::string_view campaignId = "puzzle.campaign.id";
inline constexpr std::string_view ended = "puzzle.campaign.ended";
}

struct PuzzleCampaignProgressLoadResult final
{
    PuzzleCampaignSession campaign;
    SaveLoadResult save;
    SaveCustomState additionalCustomState;
};

enum class PuzzleCampaignSaveSlotPreviewState
{
    Empty,
    Valid,
    RecoveredFromBackup,
    Invalid,
    CampaignMismatch,
};

struct PuzzleCampaignSaveSlotPreview final
{
    std::size_t slot = 0;
    PuzzleCampaignSaveSlotPreviewState state = PuzzleCampaignSaveSlotPreviewState::Empty;
    std::string savedLevelId;
    bool ended = false;
    std::uintmax_t sourceBytes = 0;
    std::uintmax_t sourcePrimaryBytes = 0;
    std::uintmax_t sourceBackupBytes = 0;
    bool sourcePrimaryExists = false;
    bool sourceBackupExists = false;
    bool sourceFingerprintComplete = true;
    std::uint64_t sourcePrimaryFingerprint = 0;
    std::uint64_t sourceBackupFingerprint = 0;
    bool primaryConflict = false;
    bool backupConflict = false;
    std::string diagnostic;
};

enum class PuzzleCampaignSaveTransferRecoveryArtifactState
{
    Missing,
    Complete,
    Partial,
    Changed,
    Unsafe,
};

enum class PuzzleCampaignSaveTransferRecoveryJournalState
{
    Missing,
    Valid,
    Unreadable,
    Unsafe,
    Oversized,
    Malformed,
};

struct PuzzleCampaignSaveTransferRecoveryArtifact final
{
    std::size_t slot = 0;
    bool backup = false;
    std::filesystem::path targetPath;
    std::uintmax_t expectedBytes = 0;
    std::uint64_t expectedFingerprint = 0;
    std::uintmax_t observedBytes = 0;
    PuzzleCampaignSaveTransferRecoveryArtifactState state =
        PuzzleCampaignSaveTransferRecoveryArtifactState::Missing;
};

struct PuzzleCampaignSaveTransferRecovery final
{
    bool present = false;
    std::filesystem::path journalPath;
    std::string campaignId;
    std::uint64_t journalFingerprint = 0;
    std::uintmax_t journalBytes = 0;
    std::vector<PuzzleCampaignSaveTransferRecoveryArtifact> artifacts;
    std::string diagnostic;
    bool journalValid = false;
    PuzzleCampaignSaveTransferRecoveryJournalState journalState =
        PuzzleCampaignSaveTransferRecoveryJournalState::Missing;
};

enum class PuzzleCampaignSaveArchivePathKind
{
    Missing,
    RegularFile,
    Directory,
    SymlinkOrReparsePoint,
    Other,
    Unavailable,
};

enum class PuzzleCampaignSaveArchiveContentMatch
{
    NotApplicable,
    ExactMatch,
    Different,
    PrefixMatch,
    Oversized,
    Unavailable,
};

struct PuzzleCampaignSaveArchiveInspection final
{
    std::filesystem::path archivePath;
    PuzzleCampaignSaveArchivePathKind pathKind = PuzzleCampaignSaveArchivePathKind::Unavailable;
    std::optional<std::uintmax_t> byteSize;
    PuzzleCampaignSaveArchiveContentMatch contentMatch =
        PuzzleCampaignSaveArchiveContentMatch::Unavailable;
    std::string diagnostic;
};

struct PuzzleCampaignSaveArchiveConflictCopyPreview final
{
    std::filesystem::path sourcePath;
    std::filesystem::path destinationPath;
    std::vector<std::uint8_t> reviewedBytes;
    // Opaque identity fields used only to reject a replaced source at confirmation time.
    std::uint64_t sourceIdentityHigh = 0;
    std::uint64_t sourceIdentityLow = 0;
    std::uint64_t sourceChangeStamp = 0;
};

struct PuzzleCampaignSaveTransferPreview final
{
    std::filesystem::path sourceDirectory;
    std::filesystem::path targetDirectory;
    std::string campaignId;
    bool targetDirectoryExists = false;
    std::vector<PuzzleCampaignSaveSlotPreview> slots;
    std::vector<std::string> unexpectedSourceArtifacts;
    std::uintmax_t sourceBytes = 0;
    std::size_t conflictingSlots = 0;
    std::size_t validSlots = 0;
    std::size_t recoveredSlots = 0;
    std::size_t invalidSlots = 0;
    std::size_t campaignMismatchSlots = 0;
    std::optional<PuzzleCampaignSaveTransferRecovery> pendingRecovery;
};

enum class PuzzleCampaignSaveTransferPreflightState
{
    Ready,
    StaleSource,
    StaleDestination,
    DestinationConflict,
    SourceNotTransferable,
    RecoveryRequired,
};

struct PuzzleCampaignSaveTransferPreflight final
{
    PuzzleCampaignSaveTransferPreflightState state = PuzzleCampaignSaveTransferPreflightState::SourceNotTransferable;
    PuzzleCampaignSaveTransferPreview currentPreview;
    std::string diagnostic;
};

enum class PuzzleCampaignSaveTransferCopyState
{
    Copied,
    Resumed,
    ConfirmationRequired,
    StaleSource,
    StaleDestination,
    DestinationConflict,
    SourceNotTransferable,
    RecoveryRequired,
    CopiedRecoveryPending,
    CancelledRolledBack,
    FailedRolledBack,
    RollbackIncomplete,
};

struct PuzzleCampaignSaveTransferCopyResult final
{
    PuzzleCampaignSaveTransferCopyState state = PuzzleCampaignSaveTransferCopyState::FailedRolledBack;
    std::size_t filesAttempted = 0;
    std::size_t filesCopied = 0;
    std::size_t filesSkipped = 0;
    std::size_t filesRolledBack = 0;
    std::uintmax_t bytesCopied = 0;
    std::vector<std::filesystem::path> remainingArtifacts;
    std::string diagnostic;
};

using PuzzleCampaignSaveTransferProgress = std::function<bool(std::size_t completedFiles,
    std::size_t totalFiles)>;

// Selects only the required campaign ID, active level ID, campaign state and Puzzle snapshot.
core::Result<void> selectPuzzleCampaignProgressFields(SaveProfile& profile);

// The caller supplies values for any additional custom fields selected by the profile.
core::Result<SaveWriteResult> savePuzzleCampaignProgress(const SaveStore& store, std::size_t slot,
    const scene::Scene& scene, const SaveProfile& profile, std::string_view gameVersion,
    const PuzzleCampaignSession& campaign, const SaveCustomState& additionalCustomState = {});

// Restores the Scene, Puzzle state and campaign session only after every field and current asset validates.
core::Result<PuzzleCampaignProgressLoadResult> loadPuzzleCampaignProgress(const SaveStore& store,
    std::size_t slot, scene::Scene& scene, const SaveProfile& profile, std::string_view gameVersion,
    const std::filesystem::path& projectRoot, std::string_view expectedCampaignId);

// Read-only inspection for a prospective same-campaign transfer. It never creates directories or copies files.
core::Result<PuzzleCampaignSaveTransferPreview> previewPuzzleCampaignSaveTransfer(
    const std::filesystem::path& sourceDirectory, const std::filesystem::path& targetDirectory,
    std::string_view expectedCampaignId);

// Read-only final check before a future copy action. Any changed source requires a new preview;
// any destination conflict blocks copying. This function never creates or writes files.
core::Result<PuzzleCampaignSaveTransferPreflight> revalidatePuzzleCampaignSaveTransferPreview(
    const PuzzleCampaignSaveTransferPreview& displayedPreview);

// A pending recovery journal is read-only. It identifies expected target artifacts but never removes them.
core::Result<PuzzleCampaignSaveTransferRecovery> inspectPuzzleCampaignSaveTransferRecovery(
    const std::filesystem::path& targetDirectory);

// Acknowledgement requires explicit confirmation and exact journal-byte revalidation.
// Windows removes by the verified file handle; unsupported platforms fail closed. Slot artifacts are untouched.
core::Result<void> acknowledgePuzzleCampaignSaveTransferRecovery(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery, bool explicitlyConfirmed);

// Read-only inspection of the deterministic archive path for a reviewed malformed journal.
// The target is never followed through a symlink/reparse point or modified.
core::Result<PuzzleCampaignSaveArchiveInspection> inspectMalformedPuzzleCampaignSaveArchive(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery);

// Creates a bounded in-memory confirmation preview for a differing regular-file occupant.
// It suggests an adjacent no-replace destination but writes nothing.
core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview> previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery,
    const PuzzleCampaignSaveArchiveInspection& displayedInspection);

// Copies the reviewed occupant byte-for-byte to the suggested adjacent path after separate confirmation.
// It retains the original, malformed journal and save slots; the copy is not trusted as an archive.
core::Result<std::filesystem::path> preserveMalformedPuzzleCampaignSaveArchiveConflict(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery,
    const PuzzleCampaignSaveArchiveConflictCopyPreview& displayedCopyPreview,
    bool explicitlyConfirmed);

// Archives the exact bounded bytes of a reviewed malformed journal and verifies the copy.
// It deliberately preserves the original journal; slot artifacts are never changed.
// Unreadable, unsafe, and oversized journals require manual inspection and are not eligible.
core::Result<std::filesystem::path> archiveMalformedPuzzleCampaignSaveTransferJournal(
    const PuzzleCampaignSaveTransferRecovery& displayedRecovery, bool explicitlyConfirmed);

// Copy valid same-ID slot artifacts only. Requires explicit caller confirmation, revalidates before
// creating the target, refuses every existing destination slot artifact, and rolls back files it created
// when an ordinary write failure or progress cancellation occurs. It does not migrate save payloads.
core::Result<PuzzleCampaignSaveTransferCopyResult> copyPuzzleCampaignSaveTransfer(
    const PuzzleCampaignSaveTransferPreview& displayedPreview, bool explicitlyConfirmed,
    PuzzleCampaignSaveTransferProgress progress = {});

// Resume only a journaled transfer whose source artifacts still exactly match the reviewed
// inventory. Complete targets are skipped; only missing targets may be created exclusively.
// Partial, changed and unsafe targets are conflicts and are never repaired or overwritten.
core::Result<PuzzleCampaignSaveTransferCopyResult> resumePuzzleCampaignSaveTransfer(
    const PuzzleCampaignSaveTransferPreview& displayedPreview, bool explicitlyConfirmed,
    PuzzleCampaignSaveTransferProgress progress = {});
}
