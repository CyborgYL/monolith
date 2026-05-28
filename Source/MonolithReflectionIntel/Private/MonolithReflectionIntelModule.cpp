// SPDX-License-Identifier: MIT
// Plan: Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence.md (Phase 1).
//
// DEVIATION (vs plan §6 row "Modify MonolithSourceSubsystem.cpp — register
// FDecisionRecordIndexer in the indexer roster"):
// UMonolithSourceSubsystem does NOT expose an indexer-roster API in the live
// codebase; it owns a single FMonolithSourceIndexer for C++ source mining
// only. To keep dependency direction correct (MonolithReflectionIntel ->
// MonolithSource is the legal direction; the reverse would be circular),
// the indexer self-bootstraps:
//   1. Once on demand from FDecisionQueryAdapter when a table miss occurs.
//   2. On FCoreUObjectDelegates::ReloadCompleteDelegate (Live Coding / hot
//      reload) so corpus changes since editor start are picked up.
// Net effect matches the plan's intent: decision_query results stay fresh,
// and the indexer runs in the same editor lifecycle as the source subsystem.

#include "MonolithReflectionIntelModule.h"
#include "Decision/FDecisionRecordIndexer.h"
#include "Decision/FDecisionQueryAdapter.h"
#include "MonolithReflectionIntelSettings.h"

#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "MonolithToolRegistry.h"
#include "SQLiteDatabase.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogMonolithReflectionIntel);

#define LOCTEXT_NAMESPACE "FMonolithReflectionIntelModule"

namespace
{
	FString GetEngineSourceDbPathStatic()
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Monolith"));
		if (Plugin.IsValid())
		{
			return Plugin->GetBaseDir() / TEXT("Saved") / TEXT("EngineSource.db");
		}
		return FPaths::ProjectPluginsDir() / TEXT("Monolith") / TEXT("Saved") / TEXT("EngineSource.db");
	}
}

void FMonolithReflectionIntelModule::StartupModule()
{
	// Explicit re-arm of the lazy-bootstrap latch on every module load. A fresh
	// module instance always starts with the latch cleared so that Live Coding
	// reloads re-attempt bootstrap if a prior attempt failed.
	bDecisionBootstrapAttempted = false;

	RegisterDecisionActions();

	// Bind hot-reload hook so the decision corpus refreshes after Live Coding /
	// UBT rebuilds. The handler opens the DB ReadWrite only briefly (the
	// indexer wipes+rewrites and closes immediately) — collision with the
	// source subsystem's ReadWrite handle is avoided by only firing on
	// reload-complete (subsystem closes its handle during reindex anyway,
	// and outside of reindex SQLite tolerates a second briefly-open RW handle).
	ReloadCompleteHandle = FCoreUObjectDelegates::ReloadCompleteDelegate.AddRaw(
		this, &FMonolithReflectionIntelModule::OnReloadComplete);

	// NO eager bootstrap on StartupModule — the source subsystem's WAL handle
	// may already be open on EngineSource.db at this point and our writer would
	// race. Decision-indexer bootstrap is LAZY:
	//   - Driven on first decision_query call (adapter checks table presence)
	//   - Driven on hot-reload via OnReloadComplete
	// First-run users see "decision tables empty" from list_decisions until
	// they either edit a source file (Live Coding) or call list_decisions
	// after running source.trigger_reindex (which fires ReloadCompleteDelegate).

	UE_LOG(LogMonolithReflectionIntel, Log,
		TEXT("Monolith — ReflectionIntel module loaded (decision_query: 5 actions)"));
}

void FMonolithReflectionIntelModule::ShutdownModule()
{
	if (ReloadCompleteHandle.IsValid())
	{
		FCoreUObjectDelegates::ReloadCompleteDelegate.Remove(ReloadCompleteHandle);
		ReloadCompleteHandle.Reset();
	}

	// CRITICAL: tear down the cached SQLite handle BEFORE module unload. Plain
	// TUniquePtr<T> member destruction order during module teardown is not
	// guaranteed; an explicit Reset() ensures FSQLiteDatabase::Close() runs
	// while the SQLiteCore module is still loaded, releasing the file lock
	// cleanly. Without this, editor exit / module reload would leak the
	// SQLite handle AND the underlying file lock on EngineSource.db.
	ResetCachedQueryDb();

	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("decision"));
}

FSQLiteDatabase* FMonolithReflectionIntelModule::GetOrOpenCachedQueryDb()
{
	const FString DbPath = GetEngineSourceDbPathStatic();

	// Fast path: handle already open at the same path and still valid.
	if (CachedQueryDb.IsValid() && CachedQueryDbPath == DbPath && CachedQueryDb->IsValid())
	{
		return CachedQueryDb.Get();
	}

	// Drop any stale handle (path-change or invalidated) before opening a new one.
	if (CachedQueryDb.IsValid())
	{
		CachedQueryDb->Close();
		CachedQueryDb.Reset();
	}

	IPlatformFile& Pf = FPlatformFileManager::Get().GetPlatformFile();
	if (!Pf.FileExists(*DbPath))
	{
		// EngineSource.db has not been bootstrapped — caller surfaces the
		// "run source.trigger_reindex" error message.
		return nullptr;
	}

	CachedQueryDb = MakeUnique<FSQLiteDatabase>();
	// ReadOnly mode — pure query side, never writes. Avoids any WAL-mode
	// silent-fail trap by NOT writing (cf. .claude/rules/scoped/cpp-code.md
	// § "Known Pitfalls": SQLite WAL + ReadOnly silent failure on Windows).
	if (!CachedQueryDb->Open(*DbPath, ESQLiteDatabaseOpenMode::ReadOnly))
	{
		CachedQueryDb.Reset();
		return nullptr;
	}
	CachedQueryDbPath = DbPath;
	return CachedQueryDb.Get();
}

void FMonolithReflectionIntelModule::ResetCachedQueryDb()
{
	if (CachedQueryDb.IsValid())
	{
		CachedQueryDb->Close();
		CachedQueryDb.Reset();
	}
	CachedQueryDbPath.Reset();
}

void FMonolithReflectionIntelModule::RegisterDecisionActions()
{
	FDecisionQueryAdapter::RegisterActions(FMonolithToolRegistry::Get());
}

void FMonolithReflectionIntelModule::OnReloadComplete(EReloadCompleteReason /*Reason*/)
{
	// Fire-and-log; never block the reload signal. Failure to refresh the
	// decision corpus is non-fatal — query handlers still return last-known data.
	FString Status;
	RunDecisionIndexerOnce(Status);
}

bool FMonolithReflectionIntelModule::RunDecisionIndexerOnce(FString& OutStatus)
{
	const UMonolithReflectionIntelSettings* Settings = UMonolithReflectionIntelSettings::Get();
	if (!Settings || !Settings->bEnableDecisionMining)
	{
		OutStatus = TEXT("RunDecisionIndexerOnce: skipped (bEnableDecisionMining=false)");
		return false;
	}

	const FString DbPath = GetEngineSourceDbPathStatic();
	IPlatformFile& Pf = FPlatformFileManager::Get().GetPlatformFile();
	if (!Pf.FileExists(*DbPath))
	{
		OutStatus = FString::Printf(
			TEXT("RunDecisionIndexerOnce: EngineSource.db not present at '%s' — bootstrap with source.trigger_reindex"),
			*DbPath);
		UE_LOG(LogMonolithReflectionIntel, Verbose, TEXT("%s"), *OutStatus);
		return false;
	}

	FSQLiteDatabase Db;
	if (!Db.Open(*DbPath, ESQLiteDatabaseOpenMode::ReadWrite))
	{
		OutStatus = FString::Printf(TEXT("RunDecisionIndexerOnce: failed to open '%s'"), *DbPath);
		UE_LOG(LogMonolithReflectionIntel, Warning, TEXT("%s"), *OutStatus);
		return false;
	}

	// Force DELETE journal mode BEFORE any CREATE TABLE / INSERT runs. Opening
	// a second handle (ours, RW) on a SQLite DB that the source subsystem may
	// have left in WAL mode is exactly the silent-failure pattern documented
	// in .claude/rules/scoped/cpp-code.md § "Known Pitfalls" (WAL + ReadOnly
	// silent failure on Windows). Mirrors the pattern at
	// MonolithSourceDatabase.cpp:124. Tolerate failure (warn + continue) per
	// project convention — other indexers also tolerate this PRAGMA result.
	if (!Db.Execute(TEXT("PRAGMA journal_mode=DELETE;")))
	{
		UE_LOG(LogMonolithReflectionIntel, Warning,
			TEXT("RunDecisionIndexerOnce: PRAGMA journal_mode=DELETE failed on '%s' (continuing)"),
			*DbPath);
	}

	TArray<FString> Roots = Settings->DecisionMarkdownRoots;
	if (Roots.Num() == 0)
	{
		Roots.Add(TEXT("Docs"));
		Roots.Add(TEXT("Plugins/Monolith/Docs"));
		Roots.Add(TEXT(".claude/rules"));
	}

	FDecisionRecordIndexer Indexer;
	const bool bOk = Indexer.Run(Db, Roots, OutStatus);
	Db.Close();
	return bOk;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithReflectionIntelModule, MonolithReflectionIntel)
