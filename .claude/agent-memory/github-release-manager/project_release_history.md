---
name: Monolith release history and workflow notes
description: Past release tags, zip sizes, what went smoothly, what to watch for next time
type: project
---

## Release history

| Version | Date | Tag | Zip size | Actions |
|---------|------|-----|----------|---------|
| 0.9.0 | 2026-03-19 | pending | — | 290 |
| 0.8.0 | 2026-03-15 | pending | — | 220 |
| 0.7.3 | 2026-03-15 | v0.7.3 | 1.7 MB | 218 |
| 0.7.2 | 2026-03-13 | v0.7.2 | — | 177 |
| 0.7.1 | 2026-03-11 | v0.7.1 | — | 177 |
| 0.7.0 | 2026-03-10 | v0.7.0 | — | 177 |
| 0.6.1 | 2026-03-10 | v0.6.1 | — | 133 |
| 0.6.0 | 2026-03-10 | v0.6.0 | — | 133 |
| 0.5.x | 2026-03-08/09 | — | — | ~122 |
| 0.2.0 | 2026-03-08 | — | — | — |
| 0.1.0 | 2026-03-07 | — | — | 119 |

## v0.9.0 release (2026-03-19)

- Version bump committed (c5f399e). DLL build pending — user will build then zip + push.
- Key changes: +69 actions (Blueprint 47→67, Material 25→47, Niagara 47→64, Animation 62→74). 60 bug fixes. 202 tests all pass.
- IKRig/Retargeter/Control Rig/AnimBP writes, full Material instance CRUD, Niagara dynamic inputs/event handlers/simulation stages.
- After UBT build: push origin/master, make_release.ps1, gh release create v0.9.0
- **Wiki DONE (2026-03-19):** All 5 pages updated and pushed to origin/master (commit ddeddb9).
  - Changelog.md, Home.md, Connecting-Your-AI.md, Tool-Reference.md, Test-Status.md
- **GitHub About section still needs update:** "220 actions" → "290 actions"

## v0.8.0 release prep (2026-03-15)

- Changelog, README, and all wiki pages updated. Version bump pending (user will bump then build DLLs).
- Key changes: native C++ source indexer (no Python), marketplace content indexing, CDO properties, project C++ indexing, 3 community PRs (NRG-Nad)
- 219 → 220 actions (trigger_project_reindex); Blueprint 46 → 47 (get_cdo_properties); Source 10 → 11
- After version bump + UBT build: commit, push, make_release.ps1, gh release create
- GitHub About section description needs update: "218 actions" → "220 actions", add "native C++ source indexer" framing
- Wiki pages updated: Changelog.md, Home.md, Connecting-Your-AI.md, Tool-Reference.md, Test-Status.md, FAQ.md, Installation.md

## v0.7.3 release notes (2026-03-15)

- Commit: 66485b4 (release: v0.7.3 — HLSL module creation, Blueprint expansion, 218 actions)
- Pushed 3 commits to origin/master (b717333..66485b4)
- Zip created via `Scripts/make_release.ps1 -Version "0.7.3"` — 1.7 MB
- GitHub release: https://github.com/tumourlove/monolith/releases/tag/v0.7.3
- Wiki updated: Changelog.md (full entry), Connecting-Your-AI.md (177→218), Test-Status.md (BP 6→46)

## Workflow notes

- `make_release.ps1` sets `"Installed": true` in the .uplugin inside the zip
- Zip lands at `D:\Unreal Projects\Leviathan\Plugins\Monolith-vX.Y.Z.zip` (one level above the plugin folder)
- Wiki local clone: `D:\Unreal Projects\Leviathan\Plugins\Monolith\wiki\` (cloned 2026-03-19, remote: git@github.com:tumourlove/monolith.wiki.git)
- After a big feature expansion, check these wiki pages for stale action counts:
  - `Connecting-Your-AI.md` — has action count in the "How It Works" section
  - `Test-Status.md` — header summary, per-module table, and footer note
  - `Tool-Reference.md` — section headers have per-module counts (was already correct for 0.7.3)
  - `Home.md` — was already correct for 0.7.3 (218 actions, 9 domains)
- Test-Status.md total row is approximate — Core has 6 test entries but only 4 unique actions

**Why:** Keeping these in sync prevents users from seeing outdated numbers on the wiki.
**How to apply:** After any release where action counts change, grep the wiki for the previous count and fix all occurrences.

## GitHub repo About section

The repo About/description is managed via `gh repo edit`. Keep it updated when action counts or major features change:
```bash
gh repo edit tumourlove/monolith --description "new description here"
gh repo edit tumourlove/monolith --add-topic "new-topic" --remove-topic "old-topic"
gh repo edit tumourlove/monolith --homepage "https://url"
```

Current state (v0.7.3, pending v0.8.0 update):
- **Description:** "MCP plugin for Unreal Engine 5.7 — gives AI assistants full read/write access to Blueprints, Materials, Niagara VFX, Animation, and more. 218 actions across 9 modules."
- **v0.8.0 target description:** "MCP plugin for Unreal Engine 5.7 — gives AI assistants full read/write access to Blueprints, Materials, Niagara VFX, Animation, and more. 220 actions, native C++ source indexer, no Python required."
- **Homepage:** https://github.com/tumourlove/monolith/wiki
- **Topics:** unreal-engine, mcp, claude-code, ai-tools, niagara, blueprint, ue5, model-context-protocol, game-development, vfx

**When to update:** After any release that changes action counts, adds major features, or adds new module domains. The description has a 350-char GitHub limit. Keep it punchy.
