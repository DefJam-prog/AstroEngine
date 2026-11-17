#pragma once

#include "astro/runtime/PuzzleCampaignSaveAdapter.hpp"

#include <functional>

namespace astro::runtime::testing
{
enum class CampaignArchiveBoundary
{
    PartialWrite,
    VerifiedBeforePublish,
    PublishedBeforeStagingCleanup,
};

using CampaignArchiveHook = std::function<void(CampaignArchiveBoundary)>;

// Private integration-test entry point. The production API does not expose or read test controls.
core::Result<std::filesystem::path> archiveMalformedCampaignJournalWithHook(
    const PuzzleCampaignSaveTransferRecovery& recovery, CampaignArchiveHook hook);
core::Result<std::filesystem::path> preserveMalformedCampaignArchiveConflictWithHook(
    const PuzzleCampaignSaveTransferRecovery& recovery,
    const PuzzleCampaignSaveArchiveConflictCopyPreview& copyPreview, CampaignArchiveHook hook);
}
