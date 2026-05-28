// SPDX-License-Identifier: MIT
// MonolithReflectionIntel — Phase 1 of Reflection Intelligence (v0.17.0).
// Hosts the deterministic markdown decision-record indexer and `decision_query`
// namespace adapter. No optional / sibling plugin deps in Phase 1, so no
// conditional gating; the module loads unconditionally for all consumers.
// Plan: Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence.md (Phase 1).

using UnrealBuildTool;

public class MonolithReflectionIntel : ModuleRules
{
	public MonolithReflectionIntel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"MonolithCore",
			"SQLiteCore",
			// `DeveloperSettings` is its OWN module (NOT part of Engine) — see
			// `.claude/rules/scoped/cpp-code.md` § Module Dependencies. Required
			// for the UDeveloperSettings-derived UMonolithReflectionIntelSettings.
			"DeveloperSettings",
			"Json",
			"JsonUtilities",
			"Projects"
		});
	}
}
