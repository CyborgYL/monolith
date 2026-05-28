// SPDX-License-Identifier: MIT
// Plan: Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence.md (Phase 1).
//
// API verification (per .claude/rules/always/ue57-api.md):
//   - `UDeveloperSettings` confirmed via source_query — lives in module
//     `DeveloperSettings` (NOT `Engine`). 14+ engine sites override
//     GetCategoryName() with the same `FName(TEXT("Plugins"))` pattern used here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MonolithReflectionIntelSettings.generated.h"

/**
 * Editor Preferences > Plugins > Monolith Reflection Intel.
 * Phase 1 surfaces the markdown decision-mining toggles. Phase 2 risk-mining
 * toggles ship stubbed `(WISHLIST)` so the schema is stable from day one.
 */
UCLASS(config=MonolithSettings, defaultconfig, meta=(DisplayName="Monolith Reflection Intel"))
class MONOLITHREFLECTIONINTEL_API UMonolithReflectionIntelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMonolithReflectionIntelSettings();

	/** Cheap, allocation-free CDO accessor. */
	static const UMonolithReflectionIntelSettings* Get();

	// ----------------------------------------------------------------
	// Decision Mining (Phase 1 — v0.17.0)
	// ----------------------------------------------------------------

	/** Mine decision records from markdown corpora during indexing. */
	UPROPERTY(EditAnywhere, config, Category="Decision")
	bool bEnableDecisionMining = true;

	/** Minimum heuristic confidence to surface a record from list_decisions. */
	UPROPERTY(EditAnywhere, config, Category="Decision", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DecisionMinConfidence = 0.6f;

	/**
	 * Markdown root paths the indexer scans. Each entry is resolved RELATIVE to
	 * the project root (FPaths::ProjectDir()) when not absolute. Empty array =
	 * sensible defaults: Docs/, Plugins/Monolith/Docs/, .claude/rules/.
	 */
	UPROPERTY(EditAnywhere, config, Category="Decision")
	TArray<FString> DecisionMarkdownRoots;

	// ----------------------------------------------------------------
	// Risk Mining (Phase 2 — v0.18 — WISHLIST stubs, currently no-op)
	// ----------------------------------------------------------------

	/** (WISHLIST) Phase 2 — enable git co-change mining. Default true once Phase 2 lands. */
	UPROPERTY(EditAnywhere, config, Category="Risk")
	bool bEnableGitCoChangeMining = false;

	/** (WISHLIST) Phase 2 — co-change window size in commits. */
	UPROPERTY(EditAnywhere, config, Category="Risk", meta=(ClampMin="10", ClampMax="500"))
	int32 MaxCoChangeWindowCommits = 50;

	// ----------------------------------------------------------------
	// UDeveloperSettings overrides
	// ----------------------------------------------------------------

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
