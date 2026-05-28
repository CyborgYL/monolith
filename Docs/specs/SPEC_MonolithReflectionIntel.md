# Monolith — MonolithReflectionIntel Module

**Parent:** [SPEC_CORE.md](../SPEC_CORE.md)
**Engine:** Unreal Engine 5.7+
**Version:** 0.17.0 (Phase 1 shipped — `decision_query`, 5 actions)

---

## 1. Purpose

`MonolithReflectionIntel` is a deterministic, $0-LLM intelligence layer that mines high-signal facts out of the project's own artefacts (markdown, git history, C++, AssetRegistry) and exposes them as MCP query actions. It exists to give AI agents structured answers to questions the project itself already knows the answer to — without spending tokens re-deriving them from raw source.

Phase 1 (shipped, v0.17.0) ships the **Decision Intelligence** slice — extracting architectural decision records from the project's markdown corpora (specs, plans, CHANGELOG, `.claude/rules/`) and serving them through the `decision_query` namespace (5 actions). The remaining three phases are planned but not yet implemented.

### Roadmap

| Phase | Status | Surface | Substrate |
|-------|--------|---------|-----------|
| 1 — Decision Intelligence | **shipped v0.17.0** | `decision_query` (5 actions) | Markdown heuristic harvest |
| 2 — Risk Intelligence | `(WISHLIST)` | `risk_query` + conditional-gate audit | Git log + LOC sweep + regex over `#if WITH_*` / `bHas*` |
| 3 — CppReflect Intelligence | `(WISHLIST)` | `cppreflect_query` + cpp↔asset edges | tree-sitter-unreal-cpp + UHT artefacts + `IAssetRegistry` |
| 4 — Network Intelligence | `(WISHLIST)` | `network_query` + audit actions + pipelines | Composes Phases 1+2+3 |

The phases are independent (Phase 2 does not depend on Phase 1; Phase 4 depends on Phase 3 reflection-edge tables for `network_query` and the audit actions). Each phase ships as its own point release.

---

## 2. Module Architecture

**Type:** `Editor`
**Loading phase:** `Default`
**Public namespace:** `decision` (5 actions in Phase 1)

`MonolithReflectionIntel` is a self-contained editor module. It owns one indexer worker (`FDecisionRecordIndexer`), one query adapter (`FDecisionQueryAdapter`), one settings UCLASS (`UMonolithReflectionIntelSettings`), and one shared SQLite schema fragment (`MonolithDecisionSchema` namespace).

### Lazy bootstrap

The module does NOT eagerly run the indexer on `StartupModule`. Two reasons:

1. The `UMonolithSourceSubsystem` may hold a ReadWrite handle on `EngineSource.db` at startup time; opening a second writer would race.
2. The decision corpus is small (~50–500 records at Leviathan scale) and tolerates lazy first-call cost.

Bootstrap fires on two events:

- **First `decision_query` call** — `FDecisionQueryAdapter::GetRawDB` checks for the `decision_records` table; if missing, it closes its ReadOnly handle, calls `FMonolithReflectionIntelModule::RunDecisionIndexerOnce` (which opens a brief ReadWrite handle, ensures schema, writes rows, closes), and reopens ReadOnly. Subsequent calls hit the cached ReadOnly handle.
- **`FCoreUObjectDelegates::ReloadCompleteDelegate`** — bound at `StartupModule`. After Live Coding or UBT-driven hot-reload, the corpus refreshes automatically so agents see decisions added to spec files in the current session without manually triggering a re-index.

The `RunDecisionIndexerOnce` entry point is idempotent — calling it repeatedly is cheap (one wipe-and-rewrite per call) and safe.

### Shutdown

`ShutdownModule` unbinds the reload delegate and calls `FMonolithToolRegistry::UnregisterNamespace("decision")` so dispatcher state stays clean on editor exit.

---

## 3. Decision Intelligence (Phase 1 — SHIPPED v0.17.0)

### 3.1 Markdown corpus harvest scope

The indexer walks `*.md` files recursively under each configured markdown root via `IFileManager::IterateDirectoryRecursively` (visitor pattern — sidesteps the `FindFilesRecursive` 6th-param `bClearFileNames=true` trap documented in `.claude/rules/scoped/cpp-code.md`).

Default roots (used when `UMonolithReflectionIntelSettings::DecisionMarkdownRoots` is empty):

- `Docs/` — project-level specs and plans
- `Plugins/Monolith/Docs/` — Monolith specs, plans, CHANGELOG, guides
- `.claude/rules/` — agent rules

Each root is resolved relative to `FPaths::ProjectDir()` unless absolute. Non-existent roots are skipped with a `Verbose` log line — never an error.

Files are read in full via `FFileHelper::LoadFileToString` and tokenised into lines via `FString::ParseIntoArrayLines` for header walk.

### 3.2 Heuristic detection

The indexer emits at most one row per markdown header (or one per file in the frontmatter-decision path). Three detection tiers with distinct confidence floors:

| Tier | Trigger | Confidence | Status default |
|------|---------|------------|----------------|
| **YAML frontmatter** | Leading `---` block with `decision: true` OR any `status:` key | `0.90` | from `status:` value (lowercased), else `accepted` |
| **ADR-style header** | Line matches `(?i)^#+\s*(?:ADR[-\s]?\d+|Architectural\s+Decision)\b` | `0.85` | `open` |
| **Header + rationale marker** | Markdown header (H2–H6 only — H1 skipped unless ADR-style) followed within 8 lines by a paragraph containing `because` / `rationale` / `evidence` / `decision:` | `0.65` | `open` |

Files matching neither tier contribute zero rows. Headers without rationale markers and without ADR shape are skipped — the indexer is conservative by design.

The `UMonolithReflectionIntelSettings::DecisionMinConfidence` floor (default `0.6`) is applied at **query time** by `decision_query("list_decisions")`, not at extraction time — every detected record is stored, then filtered on read so callers can override the floor per call.

### 3.3 SQLite schema

Tables live inside the shared `EngineSource.db` file under the `decision_` prefix so they coexist with the source-indexer's tables without name collision.

```sql
CREATE TABLE IF NOT EXISTS decision_records (
    decision_id     TEXT PRIMARY KEY,
    title           TEXT NOT NULL,
    status          TEXT NOT NULL DEFAULT 'open',
    source_path     TEXT NOT NULL,
    source_line     INTEGER NOT NULL DEFAULT 0,
    confidence      REAL NOT NULL DEFAULT 0.0,
    rationale       TEXT,
    source_mtime    INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS decision_supersedes (
    from_decision_id TEXT NOT NULL,
    to_decision_id   TEXT NOT NULL,
    PRIMARY KEY (from_decision_id, to_decision_id)
);

CREATE INDEX IF NOT EXISTS idx_decision_records_status
    ON decision_records(status);
CREATE INDEX IF NOT EXISTS idx_decision_records_source_path
    ON decision_records(source_path);
CREATE INDEX IF NOT EXISTS idx_decision_supersedes_to
    ON decision_supersedes(to_decision_id);
```

All schema statements use `CREATE ... IF NOT EXISTS` so first-run bootstrap and subsequent re-runs are both safe. Index creation failure is non-fatal (logged at `Warning`); the base tables MUST succeed or the indexer aborts.

**`decision_id` shape:** `<forward-slashed-project-relative-path>#<header-anchor>`, where the anchor is a slug derived from header text (lowercased, alphanumeric + hyphens, trailing hyphens trimmed). Frontmatter-decision rows use `#frontmatter` as the anchor. The ID is stable across reindex runs as long as the path and header text are stable.

**Wipe-and-rewrite semantics:** every `Run()` call wipes both tables and rewrites from scratch. The corpus is small enough that incremental delta-detection isn't justified; full rewrite makes "decision removed from markdown" reflect immediately. Writes occur inside a single `BEGIN TRANSACTION ... COMMIT` block with a reused prepared statement per `MeshCatalogIndexer.cpp` pattern.

### 3.4 Action surface

5 actions register under `decision` from `FDecisionQueryAdapter::RegisterActions`. All five carry `readOnlyHint: true`, `destructiveHint: false`, `idempotentHint: true` on the dispatcher annotations (v0.17.0 `tools/list` surface). All five participate in v0.17.0 universal response shaping (`_fields` / `_omit` / `_compact_json`) for free.

#### `decision_query("list_decisions", params)`

List architectural decisions filtered by source-path substring and minimum heuristic confidence. Cursor-paginated.

| Param | Type | EMonolithParamKind | Required | Default | Notes |
|-------|------|---------------------|----------|---------|-------|
| `path_filter` | string | `DiskPath` | no | `""` | Substring match against `source_path` (project-relative). `\` → `/` rewritten by dispatcher with surfaced warning. |
| `min_confidence` | number | `Other` | no | `0.6` | Floor in `[0, 1]`. Settings default is also `0.6`; per-call override wins. |
| `status` | string | `Other` | no | `""` | Exact match — typical values: `open`, `accepted`, `superseded`, `deprecated`, `draft`. |
| `limit` | integer | `Other` | no | `50` | Page size. Hard cap `200`. |
| `cursor` | string | `Other` | no | `""` | Opaque base64+JSON cursor from a prior `next_cursor`. Restart pagination by omitting. |

**Response:**

```json
{
  "decisions": [
    {
      "decision_id": "Plugins/Monolith/Docs/SPEC_CORE.md#some-anchor",
      "title": "Some Architectural Decision",
      "status": "open",
      "source_path": "Plugins/Monolith/Docs/SPEC_CORE.md",
      "source_line": 142,
      "confidence": 0.85,
      "rationale": "Rationale paragraph if one was mined.",
      "source_mtime": 1717094400
    }
  ],
  "total_estimate": 47,
  "next_cursor": "<opaque>"
}
```

`total_estimate` is emitted on page 0 only (one `COUNT(*)` per filter set). Subsequent pages carry the cached count inside the cursor. `next_cursor` is omitted on the last page.

#### `decision_query("get_decision", params)`

Fetch one record by stable id.

| Param | Type | Required |
|-------|------|----------|
| `decision_id` | string | yes |

**Response:** `{ "decision": <row-or-null> }` — `decision` is `null` when the id is unknown.

#### `decision_query("list_stale", params)`

List decisions whose source markdown has not been modified within `max_age_days` days. Useful for spec-drift detection.

| Param | Type | EMonolithParamKind | Required | Default | Notes |
|-------|------|---------------------|----------|---------|-------|
| `max_age_days` | integer | `Other` | yes | — | Positive only. Compared against source-file mtime in UTC. |
| `path_filter` | string | `DiskPath` | no | `""` | Substring match. |
| `limit` | integer | `Other` | no | `50` | Hard cap `200`. |
| `cursor` | string | `Other` | no | `""` | Opaque cursor. |

**Response:**

```json
{
  "stale_decisions": [ /* row objects */ ],
  "cutoff_unix": 1714502400,
  "next_cursor": "<opaque>"
}
```

Rows are ordered by `source_mtime ASC` (oldest first). Records with `source_mtime = 0` (mtime unavailable) are excluded — they cannot be honestly classified.

#### `decision_query("find_supersession_chain", params)`

Walk supersedes edges outward from a starting decision. Returns the ordered chain of decisions the start id transitively supersedes.

| Param | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `decision_id` | string | yes | — | Start of the walk. |
| `depth` | integer | no | `10` | Maximum traversal depth. Hard cap `50`. |

**Response:**

```json
{
  "start": "<decision_id>",
  "chain": [
    { "from": "<id>", "to": "<id>", "depth": 1 },
    { "from": "<id>", "to": "<id>", "depth": 2 }
  ],
  "truncated": false
}
```

`truncated: true` indicates the walk hit `depth` while frontier nodes remained — call again with higher `depth` if needed. Cycle protection via a visited set.

#### `decision_query("find_referent_decisions", params)`

Inverse of `find_supersession_chain` — list decisions that explicitly supersede the given id (the records that replaced it).

| Param | Type | Required |
|-------|------|----------|
| `decision_id` | string | yes |

**Response:**

```json
{
  "decision_id": "<id>",
  "referent_decisions": [ /* full row objects */ ]
}
```

Rows ordered by `source_path, source_line`.

### 3.5 ReadOnly fallback to `EngineSource.db`

`FDecisionQueryAdapter::GetRawDB` does NOT share `UMonolithSourceSubsystem`'s in-memory `FSQLiteDatabase` handle. The subsystem does not expose a `GetRawDatabase()` accessor on `FMonolithSourceDatabase` at the time of Phase 1 implementation, so the adapter opens its own ReadOnly handle on the same `EngineSource.db` file.

SQLite tolerates concurrent readers fine when the database is opened with `journal_mode=DELETE` (Monolith's default — see the WAL silent-fail trap in `Docs/references/UE57Gotchas.md`). The subsystem's ReadWrite handle and the adapter's ReadOnly handle coexist safely:

- ReadOnly takes no write lock; queries never block writes.
- The indexer (`RunDecisionIndexerOnce`) opens a brief ReadWrite handle only when the table is missing on first call. The subsystem closes its handle during full reindex, so the brief overlap is non-contentious.

**Migration note:** if a `GetRawDatabase()` accessor is added to `FMonolithSourceDatabase` in a later release, the adapter SHOULD migrate to the shared handle and `MonolithReflectionIntel.Build.cs` SHOULD re-add `MonolithSource` as a `PrivateDependencyModuleNames` entry. As of Phase 1, the build deps deliberately exclude `MonolithSource` to keep the dependency direction one-way.

The handle is cached statically in the adapter file scope. The cache rebuilds on path change or `IsValid()` failure.

### 3.6 Staleness detection

`source_mtime` is captured via `IFileManager::Get().GetTimeStamp(path)` and stored as a UTC Unix timestamp in the `decision_records` table. `FDateTime::MinValue()` returns sentinel `0` — those rows are excluded from `list_stale` so a filesystem error doesn't masquerade as fresh data.

`list_stale` computes the cutoff at query time: `cutoff = utc_now - max_age_days * 86400`. The SQL is `WHERE source_mtime > 0 AND source_mtime < ?`.

### 3.7 Test coverage

Four automation tests under `Monolith.ReflectionIntel.Decision.*` (`EditorContext | EngineFilter` flags):

| Test | Asserts |
|------|---------|
| `SchemaBootstrap` | Empty-corpus `Run()` succeeds; `decision_records` and `decision_supersedes` tables exist after the call. |
| `HeuristicAccuracy` | ≥4 rows ingested from the 5-file fixture corpus; `03_non_decision.md` contributes zero rows. |
| `SupersessionChain` | ≥1 edge in `decision_supersedes` after indexing the fixture corpus (file `02` carries two `Supersedes:` lines). |
| `StalenessFlag` | A fixture file copied to `FPaths::AutomationTransientDir()` and aged by 60 days via `IFileManager::SetTimeStamp` shows up in a 30-day cutoff query. |

Fixture corpus under `Source/MonolithReflectionIntel/Private/Tests/Fixtures/DecisionCorpus/` (5 markdown files: `01_decision_with_rationale.md`, `02_decision_with_supersedes.md`, `03_non_decision.md`, `04_yaml_frontmatter.md`, `05_adr_style.md`).

Disposable test DBs are created at `FPaths::AutomationTransientDir() / "decision-test-<guid>.db"` via `ESQLiteDatabaseOpenMode::ReadWriteCreate` and deleted at test teardown — the real `EngineSource.db` is never touched.

Run via `editor_query("run_automation_tests", "Monolith.ReflectionIntel.Decision")`.

---

## 4. Risk Intelligence (Phase 2 — WISHLIST)

**Planned namespace:** `risk_query` + a `source_query("audit_module_dep_reality")` audit + a `pipeline_query("pr_review")` composer.

**Substrate:** git-log mining via `FPlatformProcess::CreateProc` against the nested git repos (Monolith + Resonance + any project-side git), regex sweep for `#if WITH_*` / `bHas*` conditional gates across `.cpp` / `.h` / `.Build.cs`.

**Planned action surface:**

- `risk_query("get_hotspot_score", file_path)` — churn × complexity score for any indexed source file.
- `risk_query("get_cochange_pairs", file_path)` — files that frequently change together with the given file. MUST use cursor pagination.
- `risk_query("get_file_churn", file_path)` — per-file churn history.
- `risk_query("get_release_window_hotspots")` — hotspots most likely to bite in the next release window.
- `source_query("audit_module_dep_reality", module_root)` — flag UPROPERTY edits whose `Build.cs` companion is missing (the `feedback_softptr_uproperty_needs_module_dep.md` bug class).
- `pipeline_query("pr_review", changed_files[])` — composer that bundles risk + decision + module-dep-reality audits into one payload.

**Planned tables:** `git_cochange_pairs`, `git_file_churn`, `risk_hotspot_scores`, `reflect_conditional_gates`.

Settings stubs already shipped in `UMonolithReflectionIntelSettings` (`bEnableGitCoChangeMining`, `MaxCoChangeWindowCommits`) — both currently no-op.

Implementation plan: `Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence.md` Phase 2.

---

## 5. CppReflect Intelligence (Phase 3 — WISHLIST)

**Planned namespace:** `cppreflect_query` + cpp↔asset edge tables.

**Substrate:** `tree-sitter-unreal-cpp` for header parsing + UHT artefacts (`Intermediate/Build/.../Inc/.../*.generated.h`) for the canonical reflected surface + `IAssetRegistry` for asset cross-references.

**Planned action surface (illustrative):**

- `cppreflect_query("get_uclass", class)` — full UPROPERTY / UFUNCTION / interface inventory for a UCLASS.
- `cppreflect_query("list_uproperties", class)` — paginated UPROPERTY list. SHOULD use cursor pagination.
- `cppreflect_query("find_interface_impls", interface)` — every UCLASS implementing the given UINTERFACE.

These tables are the prerequisite substrate for Phase 4's `network_query` and the seven audit actions.

Implementation plan: `Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence.md` Phase 3.

---

## 6. Network Intelligence (Phase 4 — WISHLIST)

**Planned namespace:** `network_query` + seven audit actions across `material_query` / `niagara_query` / `animation_query` / `source_query` + `pipeline_query("release_readiness")` composer.

**Substrate:** Phases 1+2+3 composed — decision corpus + risk scores + reflection edges.

**Planned action surface (illustrative):**

- `network_query("audit_replicated_properties", module_filter)` — project-wide replication audit.
- `material_query("audit_orphan_materials", path_prefix)` — materials with no usage edges in the asset graph.
- `niagara_query("audit_orphan_emitters", path_prefix)` — emitters with no system parents.
- `animation_query("audit_thread_safety", anim_bp)` — AnimBP math nodes touched from `BlueprintThreadSafeUpdateAnimation`.
- `pipeline_query("release_readiness")` — bundles all audits + Phase 1-3 reads into a single release-gate payload.

Implementation plan: `Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence.md` Phase 4.

---

## 7. Dependencies

`Plugins/Monolith/Source/MonolithReflectionIntel/MonolithReflectionIntel.Build.cs`:

| Deps | Type |
|------|------|
| `Core`, `CoreUObject`, `Engine` | `PublicDependencyModuleNames` |
| `MonolithCore`, `SQLiteCore`, `DeveloperSettings`, `Json`, `JsonUtilities`, `Projects` | `PrivateDependencyModuleNames` |

`DeveloperSettings` is its own module (NOT part of `Engine`) — required for the `UDeveloperSettings`-derived `UMonolithReflectionIntelSettings`. Documented in `.claude/rules/scoped/cpp-code.md` § Module Dependencies; LNK2019 trap if omitted.

**No dependency on `MonolithSource`** — the adapter opens its own ReadOnly handle on `EngineSource.db` rather than sharing the subsystem's in-memory handle. Re-add `MonolithSource` if a future release adds a `GetRawDatabase()` accessor on `FMonolithSourceDatabase` (see §3.5).

No conditional-gate `WITH_*` macros in Phase 1 — the module loads unconditionally for all Monolith consumers and contributes 5 actions to every install.

---

## 8. Configuration

**Editor location:** Editor Preferences → Plugins → "Monolith Reflection Intel"
**INI file:** `Config/MonolithSettings.ini`
**Section:** `[/Script/MonolithReflectionIntel.MonolithReflectionIntelSettings]`

| Setting | Default | Category | Description |
|---------|---------|----------|-------------|
| `bEnableDecisionMining` | `true` | Decision | Mine decision records from markdown corpora during indexing. When `false`, `RunDecisionIndexerOnce` skips with a status string. |
| `DecisionMinConfidence` | `0.6` | Decision | Floor in `[0, 1]` applied at query time by `list_decisions`. Per-call `min_confidence` parameter overrides this. |
| `DecisionMarkdownRoots` | `[]` | Decision | Project-relative directory paths to scan. Empty array uses defaults: `Docs/`, `Plugins/Monolith/Docs/`, `.claude/rules/`. |
| `bEnableGitCoChangeMining` | `false` | Risk | `(WISHLIST)` Phase 2 toggle — currently no-op. |
| `MaxCoChangeWindowCommits` | `50` | Risk | `(WISHLIST)` Phase 2 — currently no-op. Clamped `[10, 500]`. |

`UMonolithReflectionIntelSettings::Get()` returns the cached CDO — cheap, allocation-free.

`UDeveloperSettings::GetCategoryName()` returns `"Plugins"` so the panel groups with other Monolith settings.

---

## 9. Threading Model

- **`FDecisionRecordIndexer::Run`** runs on whatever thread invoked `FMonolithReflectionIntelModule::RunDecisionIndexerOnce`. In practice that is the game thread (first-call adapter path) or whichever thread fired `FCoreUObjectDelegates::ReloadCompleteDelegate` (Live Coding fires this on the game thread). The indexer is single-threaded by construction; SQLite ops use a single `FSQLiteDatabase` handle that lives only for the duration of `Run`.
- **`FDecisionQueryAdapter::*` handlers** run on the game thread under `FMonolithToolRegistry::ExecuteAction`. All five handlers are pure read paths against the cached ReadOnly handle — no mutation, no async work, no `ParallelFor`.
- The cached ReadOnly `FSQLiteDatabase*` in `GetRawDB` is file-scope static. Phase 1 adapter usage is game-thread-only, so the cache is not lock-protected. If the surface ever fans out to background threads, add an `FCriticalSection` around the cache check/replace.
- No render-thread work. No `UPROPERTY(Replicated)`. No `Server`/`Client`/`NetMulticast` UFUNCTIONs. Editor-only by design.

---

## 10. Cross-References

- **Design spec:** `Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence-design.md`
- **Implementation plan:** `Plugins/Monolith/Docs/plans/2026-05-28-reflection-intelligence.md`
- **Parent spec:** [`SPEC_CORE.md`](../SPEC_CORE.md) — see §3 Module Reference and §12 Action Count Summary
- **MCP reference:** `Docs/references/MCP.md` — `decision_query` row
- **C++ conventions:** `.claude/rules/scoped/cpp-code.md` — module dep gotchas (`DeveloperSettings`, `FindFilesRecursive` 6th-param, SQLite WAL trap)
- **API verification log:** `Docs/references/UE57Gotchas.md`
