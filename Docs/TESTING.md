# Monolith — Testing Reference

Last updated: 2026-03-22

---

## Test Environment

- **Engine:** Unreal Engine 5.7
- **Project:** Any UE 5.7+ project with Monolith installed
- **Plugin location:** `YourProject/Plugins/Monolith/`
- **MCP endpoint:** `http://localhost:9316/mcp`

---

## How to Test

### Action Test Template

```python
import http.client, json

def test_action(namespace, action, params=None):
    conn = http.client.HTTPConnection("localhost", 9316)
    if namespace == "monolith":
        tool_name = f"monolith_{action}"
        args = params or {}
    else:
        tool_name = f"{namespace}_query"
        args = {"action": action}
        if params:
            args["params"] = params

    body = json.dumps({
        "jsonrpc": "2.0", "id": 1,
        "method": "tools/call",
        "params": {"name": tool_name, "arguments": args}
    })
    conn.request("POST", "/mcp", body, {"Content-Type": "application/json"})
    resp = conn.getresponse()
    result = json.loads(resp.read())
    conn.close()
    return result
```

---

## Current Status: ALL 340 ACTIONS PASS

All 10 modules fully tested. Polish pass 2026-03-14 added 4 Niagara actions + get_system_property (213→218). 2026-03-15: HLSL module/function creation tested end-to-end (CPU + GPU), 3 bugs fixed (input exposure, dot validation, numeric index lookup). 2026-03-17: Blueprint module waves 2-7 full test pass (48/48 + 17 retests). 21 bugs found and fixed during testing session. Total actions now 278. 2026-03-17: Material module full test pass (44/44 + 11 retests). 11 bugs found and fixed. 2026-03-18: Niagara module full test pass (37/37 + 8 retests). 16 bugs found and fixed. 2026-03-18: Animation Wave 8-10 full test pass (33/33 + 4 retests). 12 bugs found and fixed. 2026-03-22: MonolithUI module full test pass (42/42). All 8 action classes verified.

---

## Bugs Fixed (2026-03-17 — Blueprint Wave Testing)

| Module | Issue | Fix | Verified |
|--------|-------|-----|----------|
| Blueprint | `resolve_node` crash: transient node with no owning Blueprint (FindBlueprintForNodeChecked assert) | Temp Blueprint used as outer | PASS |
| Blueprint | `validate_blueprint` crash: null deref on orphaned node graph name | Null-safe GetGraph() check | PASS |
| Blueprint | `set_event_dispatcher_params` crash: PinsToRemove loop desync during iteration | Safe iteration via TSharedPtr copy | PASS |
| Blueprint | `rename_component` crash: NAME_None input triggers engine check() | Guard rejects empty name upfront | PASS |
| Blueprint | `create_blueprint` crash: IsValidChecked null assert on uncompiled GeneratedClass during save | Compile before save + FullyLoad + SaveLoadedAsset | PASS |
| Blueprint | `promote_pin_to_variable` crash risk: MarkBlueprintAsStructurallyModified before rewiring invalidates pointers | Deferred structural mark to after rewiring | PASS |
| Blueprint | `add_variable` silently mutated existing variable on name collision | Pre-check BP->NewVariables, return error if name taken | PASS |
| Blueprint | `add_event_node` allowed duplicate custom event names | Shared HasCustomEventNamed helper — both add_event_node and add_node paths guarded | PASS |
| Blueprint | `set_event_dispatcher_params` FAIL: no FunctionEntry in fresh dispatcher graph | Added CreateFunctionGraphTerminators + AddExtraFunctionFlags + MarkFunctionEntryAsEditable | PASS |
| Blueprint | `search_functions` returned 0 results for multi-word queries | Split query on spaces, AND all tokens | PASS |
| Blueprint | `validate_blueprint` missing: unimplemented interfaces and duplicate events not detected | Added both checks | PASS |
| Blueprint | `add_timeline` flags only set on node, not UTimelineTemplate | Flags set on UTimelineTemplate too | PASS |
| Blueprint | Dispatcher reference detection used substring matching (Contains) | Exact FName comparison | PASS |
| Blueprint | `connect_pins` silently auto-converted incompatible pins | Warning field added to response | PASS |
| Blueprint | `batch_execute` returned cryptic error when "op" key missing | Helpful error message added | PASS |
| Blueprint | `replication_condition` rejected COND_ prefix values | Auto-strip COND_ prefix | PASS |
| Blueprint | `add_nodes_bulk` omitted node positions from response | Positions included in per-node result | PASS |
| Blueprint | `scaffold_interface_implementation` returned 0 functions with no explanation | Note field added explaining why count is 0 | PASS |
| Blueprint | `get_interface_functions` error hint pointed to wrong naming convention | Updated to mention C++ I-prefix and BP naming | PASS |
| Blueprint | `add_node` CustomEvent bypassed duplicate guard (separate code path from add_event_node) | Shared HasCustomEventNamed helper applied to both paths | PASS |
| Blueprint | `create_blueprint` cold-path duplicate not detected via Asset Registry | Asset Registry guard added | PASS |

---

## Bugs Fixed (2026-03-17 — Material Wave Testing)

| Module | Issue | Fix | Verified |
|--------|-------|-----|----------|
| Material | `get_material_properties` missing `fully_rough` and `cast_shadow_as_masked` readback | Added bool fields to response | PASS |
| Material | `build_function_graph` type fallback warning said "Scalar" but defaulted to Vector3 | Fixed fallback to actually use Scalar | PASS |
| Material | `build_material_graph` connections using user-provided `name` fields failed (`. -> .`) | Added name→auto-name resolution; added `from_expression`/`to_expression` key aliases | PASS |
| Material | `build_material_graph` outputs array only accepted `from` key, not `from_expression` | Added `from_expression` alias in outputs parser | PASS |
| Material | `list_material_instances` no warning that unsaved assets are invisible to Asset Registry | Added `note` field about Asset Registry visibility | PASS |
| Material | `build_function_graph` required `FunctionInput_Vector3` — no shorthands | Added aliases: float/float2/float3/float4/bool/scalar/vec2/vec3/vec4/texture2d/texturecube | PASS |
| Material | `auto_layout` minimal response — no indication what changed | Added `positions_changed` field | PASS |
| Material | `get_instance_parameters` `total_overrides` counted non-overridden switch entries | Fixed to only count params where `bOverride == true` | PASS |
| Material | `clear_instance_parameter` type="all" underreported `cleared_count` — didn't count static switches | Added explicit static switch clearing after `ClearAllMaterialInstanceParameters()` | PASS |
| Material | `set_instance_parameters` response only had `set_count` — no per-param detail | Added `results` array with name/type/success per param | PASS |
| Material | `get_instance_parameters` `StaticParametersRuntime` is private in UE 5.7 — compile error | Changed to public `GetStaticParameters()` API | PASS |

---

## Bugs Fixed (2026-03-18 — Niagara Wave Testing)

| # | Action | Issue | Fix |
|---|--------|-------|-----|
| 1 | `add_event_handler` | Crash: null Script pointer — direct array push without creating UNiagaraScript | Proper script creation via NewObject + SetUsage + AddEventHandler API |
| 2 | `add_simulation_stage` | Crash risk: no null check on emitter | Added null guard |
| 3 | `add_simulation_stage` | Bug: stage_name param never applied (always "None") | Added SimulationStageName assignment |
| 4 | `create_emitter` | Bug: missing handle_id in response | Added handle_id field |
| 5 | `create_emitter` | Bug: GPU path result silently dropped | Added gpu_warning field |
| 6 | `export_system_spec` | Bug: missing "asset" field on emitter objects | Added emitter asset path |
| 7 | `add_dynamic_input` | Bug: null check after EndTransaction | Reordered — null check before EndTransaction |
| 8 | `set_fixed_bounds` | UX: disable path required min/max params | Min/max only required when enabling |
| 9 | `validate_system` | UX: mesh renderer not checked | Added empty meshes warning |
| 10 | `configure_data_interface` | UX: ImportText failures silently vanished | Added properties_failed array |
| 11 | `configure_data_interface` | UX: success:true when all properties fail | Returns error + available_properties list |
| 12 | `export_system_spec` | UX: include_values misses data input overrides | Added inputs_note explaining limitation |
| 13 | `add_dynamic_input` | UX: returned inputs only had static switches | Now includes data inputs (Minimum, Maximum, etc.) |
| 14 | `search_dynamic_inputs` | UX: type filter passed unknown types through | Unknown types now excluded when filter specified |
| 15 | `set_renderer_material` | UX: can't clear material | Accepts ""/"none" to clear |
| 16 | `validate_system` | Blocked: missing material test impossible without clear path | Unblocked by fix #15 |

---

## Bugs Fixed (2026-03-18 — Animation Wave 8-10 Testing)

| Module | Issue | Fix | Verified |
|--------|-------|-----|----------|
| Animation | `add_transition` CastChecked crash on null schema | Safe Cast + null check | PASS |
| Animation | `get_abp_linked_assets` empty arrays (wrong path type for AR lookup) | GetAssetsByPackageName instead of GetAssetByObjectPath | PASS |
| Animation | `add_ik_solver` nested transactions (4-5 undos per operation) | Removed outer transaction — controller manages own | PASS |
| Animation | `get_retargeter_info` multi-op retargeters return 0 chain mappings | Iterate target chains + GetSourceChain per chain | PASS |
| Animation | `add_state_to_machine` BoundGraph null = silent rename failure | Null check + error return | PASS |
| Animation | `set_transition_rule` MakeLinkTo bypasses schema | Use TryCreateConnection via rule graph schema | PASS |
| Animation | `add_ik_solver` SetStartBone failure silently dropped | Capture return + warning in response | PASS |
| Animation | `add_ik_solver` goals silently discarded on empty skeleton | Guard for empty skeleton + warning | PASS |
| Animation | `add_control_rig_element` parent_type defaults to child type | Default to bone when parent specified | PASS |
| Animation | `get_abp_variables` type field vague for struct/object | Include PinSubCategoryObject name | PASS |
| Animation | `get_retargeter_info` single-op note no longer needed | Removed (multi-op readback fixed) | PASS |
| Animation | ABP write experimental gate removed | All CVar checks + enable_experimental_writes action removed | PASS |

---

## MonolithUI — Full Test Pass (2026-03-22)

| Action Class | Actions | Test | Result | Notes |
|-------------|---------|------|--------|-------|
| FMonolithUIActions | 7 | Widget CRUD: create_widget_blueprint, get_widget_tree, add_widget, remove_widget, set_widget_property, compile_widget, list_widget_types | **7/7 PASS** | Create, inspect, modify, compile cycle verified |
| FMonolithUISlotActions | 3 | Slot ops: set_slot_property, set_anchor_preset, move_widget | **3/3 PASS** | Anchor presets, reparenting verified |
| FMonolithUITemplateActions | 8 | Templates: create_hud_element (3 types), create_menu, create_settings_panel, create_dialog, create_notification_toast, create_loading_screen, create_inventory_grid, create_save_slot_list | **8/8 PASS** | All 8 template types scaffold correctly |
| FMonolithUIStylingActions | 6 | Styling: set_brush, set_font, set_color_scheme, batch_style, set_text, set_image | **6/6 PASS** | Brush, font, color, text, image all apply |
| FMonolithUIAnimationActions | 5 | Animation: list_animations, get_animation_details, create_animation, add_animation_keyframe, remove_animation | **5/5 PASS** | Full animation CRUD cycle |
| FMonolithUIBindingActions | 4 | Binding: list_widget_events, list_widget_properties, setup_list_view, get_widget_bindings | **4/4 PASS** | Event and property inspection verified |
| FMonolithUISettingsActions | 5 | Settings: scaffold_game_user_settings, scaffold_save_game, scaffold_save_subsystem, scaffold_audio_settings, scaffold_input_remapping | **5/5 PASS** | All 5 scaffolds generate valid C++ |
| FMonolithUIAccessibilityActions | 4 | Accessibility: scaffold_accessibility_subsystem, audit_accessibility, set_colorblind_mode, set_text_scale | **4/4 PASS** | Audit reports font/focus/nav issues correctly |
| **Total** | **42** | | **42/42 PASS** | All UI actions pass on first run |

---

## Known Issues

| Module | Issue | Status |
|--------|-------|--------|
| Niagara | `get_system_diagnostics` reported false DataMissing errors on emitter spawn/update scripts — these are editor-only source graphs inlined into system scripts, never independently compiled | FIXED (2026-03-13): changed `GetScripts(false)` → `GetScripts(true)` |
| Niagara | `add_emitter` required full object path suffix (e.g. `.Fountain`) and silently failed on wrong param names (`template`, `emitter_path`) | FIXED (2026-03-13): switched to `LoadAssetByPath`, added param aliases |
| Niagara | `create_system_from_spec` read `save_path` from inside `spec` only — users passing it at top-level got misleading "save_path required" error | FIXED (2026-03-13): accepts at both levels |
| Niagara | `create_system` template param used raw `LoadObject` — bare paths failed with "Failed to load template" | FIXED (2026-03-13): switched to `LoadAssetByPath` |

---

## Bugs Fixed (2026-03-13 — Session 4)

| Module | Issue | Fix | Verified |
|--------|-------|-----|----------|
| Core | Registry dispatched handlers without checking required params — callers got handler-specific errors instead of "missing param X" | `FMonolithToolRegistry::Execute()` now validates required schema params before dispatch. Returns error listing missing + provided keys. Skips `asset_path` (handled by `GetAssetPath()`) | PASS — missing params return descriptive error immediately |
| Niagara | Module write actions only accepted `module_node` and `input` — any variation caused "module not found" or silent fail | Added aliases: `module_node` → also accepts `module_name`, `module`; `input` → also accepts `input_name` | PASS — all alias forms work |
| Material | `set_expression_property` called `PostEditChange()` with no args — material graph didn't rebuild, editor display didn't update | Changed to `PostEditChangeProperty(FPropertyChangedEvent(Prop))` with actual property pointer | PASS — scalar param DefaultValue change reflects immediately in editor |
| Material | `set_material_property`, `create_material`, `delete_expression`, `connect_expressions` missing `PreEditChange`/`PostEditChange` cycle | Added `Mat->PreEditChange(nullptr)` + `Mat->PostEditChange()` after each write | PASS — changes visible without manual recompile |
| Core | `tools/list` response had no param info — AI had to call `monolith_discover` to see param names | `HandleToolsList()` now embeds per-action param schemas in `params` property description: `*name(type)` per param, `*` = required | PASS — Claude Code sees param names from tool list |

## Bugs Fixed (2026-03-13 — Session 2)

| Module | Issue | Fix | Verified |
|--------|-------|-----|----------|
| Blueprint | `add_component` ignores `component_name` param — schema registered as `"name"`, handler reads `"name"`, but all other component actions use `"component_name"` | Changed schema + handler to `"component_name"` (2 lines) | PASS — component created with correct name |
| Blueprint | `create_blueprint` crashes (assert) if Blueprint already exists at path — `FindObject` assert in `FKismetEditorUtilities::CreateBlueprint` | Added existence check before `CreatePackage`, returns error instead of crashing | PASS — returns graceful error |
| Niagara | `set_module_input_value` and `set_module_input_binding` can't reach data inputs (SpawnRate, Color, Velocity, etc.) — uses local helper that only sees static switch pins, not engine's `GetStackFunctionInputs` | Replaced `MonolithNiagaraHelpers::GetStackFunctionInputs` with `FNiagaraStackGraphUtilities::GetStackFunctionInputs` + `FCompileConstantResolver` + `Module.` prefix stripping (matches `get_module_inputs` read path) | PASS — float and vector3f inputs set successfully |

## Bugs Fixed (2026-03-13 — Session 1)

| Module | Issue | Fix |
|--------|-------|-----|
| Blueprint | `remove_local_variable` — `GetLocalVariablesOfType` with empty type matches nothing | Direct `UK2Node_FunctionEntry::LocalVariables` array walk |
| Blueprint | 3 deprecation warnings: `Pop(false)`, 2x `FName`-based interface APIs | `EAllowShrinking::No`, `GetClassPathName()` |
| Core | Claude Code sends `params` as JSON string, not nested object — all `_query` tools got empty args | Added `FJsonSerializer::Deserialize` fallback for string-typed params in `HandleToolsCall` |
| Niagara | `set_emitter_property` stale bug entry — was already fixed in prior session | Removed from known bugs |
| Plugin | Missing plugin deps: `EditorScriptingUtilities`, `PoseSearch` | Added to `Monolith.uplugin` Plugins list |
| Docs | Action count 217 was wrong — PoseSearch 5 already included in animation 62 | Corrected to 212 everywhere |

---

## Niagara — HLSL Module Tests (2026-03-15)

| Action | Test | Result | Notes |
|--------|------|--------|-------|
| `create_module_from_hlsl` | Simple float input, CPU sim | PASS | Asset created, opens in editor, typed pin visible |
| `create_module_from_hlsl` | Multi-type inputs (vec3 + float), CPU sim | PASS | Multiple typed pins, correct types |
| `create_module_from_hlsl` | GPU sim target | PASS | Module compiles when added to GPU sim emitter; HLSL visible in compiled GPU shader |
| `create_function_from_hlsl` | Reusable function, CPU | PASS | Asset created, typed pins visible, script usage = Function |
| `get_module_inputs` | CustomHlsl module | PASS | Fallback reads FunctionCall typed pins directly when GetStackFunctionInputs returns empty; returns 3 typed inputs |
| `set_module_input_value` | CustomHlsl module | PASS | Sets pin DefaultValue directly on FunctionCall node |
| Dot validation | Input name with `.` (e.g. `Module.Color`) | PASS | Rejected with clear error message before asset creation |
| Dot validation | Output name with `.` | PASS | Rejected with clear error message before asset creation |

**Bugs fixed this session:**
1. Numeric index lookup — module lookup by position index was broken
2. Input exposure fallback — `get_module_inputs` now falls back to FunctionCall typed pins when engine helper returns empty for CustomHlsl modules
3. Dot validation — input/output names containing `.` now rejected upfront with descriptive error (dots break HLSL tokenizer and Niagara ParameterMap aliasing)

---

## Niagara — Polish Pass Tests (2026-03-14)

| Action | Test | Result | Notes |
|--------|------|--------|-------|
| `set_system_property` | Set WarmupTime on existing system | PASS | System-level property set via reflection |
| `set_system_property` | Set bDeterminism on existing system | PASS | Bool property accepted |
| `set_static_switch_value` | Set static switch on module | PASS | Returns blend mode validation warnings where applicable |
| `list_module_scripts` | Search by keyword | PASS | Returns matching asset paths from content browser |
| `list_module_scripts` | Empty keyword returns all | PASS | Broad search supported |
| `list_renderer_properties` | List properties on Sprite renderer | PASS | Returns property names, types, current values |
| `list_renderer_properties` | Missing renderer param returns error | PASS | Descriptive error on bad renderer name |

---

## Niagara — Session 3 Tests (2026-03-13)

| Action | Test | Result | Notes |
|--------|------|--------|-------|
| `get_system_diagnostics` | Basic output on existing system | PASS | Reports errors, warnings, emitter stats, sim target, op/register counts |
| `get_system_diagnostics` | DataMissing detection | PASS | Detects scripts with no compiled bytecode |
| `get_system_diagnostics` | GPU+dynamic bounds warning | PASS | Warns when GPU emitter uses dynamic bounds mode |
| `get_system_diagnostics` | CalculateBoundsMode=Fixed clears warning | PASS | Warning disappears when bounds set to Fixed |
| `set_emitter_property` | SimTarget CPU→GPU | PASS | Recompile triggered, GPU Compute Script appears |
| `set_emitter_property` | SimTarget GPU→CPU | PASS | Round-trip works, VM bytecode restored (181/95 ops) |
| `set_emitter_property` | CalculateBoundsMode property | PASS | New property accepted, Fixed/Dynamic values work |
| `request_compile` | force + synchronous params | PASS | New params accepted |

---

## Blueprint — Wave 5 Tests (2026-03-17)

| Action | Test | Result | Notes |
|--------|------|--------|-------|
| `scaffold_interface_implementation` | Add interface to Actor BP | PASS | Stub graphs created |
| `scaffold_interface_implementation` | Already-implemented interface returns already_implemented=true | PASS | — |
| `add_timeline` | Create timeline in Actor BP event graph | PASS | GUID linkage verified, compiles clean |
| `add_timeline` | Reject timeline in function graph | PASS | Returns error as expected |
| `add_timeline` | Auto-generated name when timeline_name omitted | PASS | — |
| `add_event_node` | BeginPlay alias → ReceiveBeginPlay override | PASS | — |
| `add_event_node` | Tick alias → ReceiveTick override | PASS | — |
| `add_event_node` | Unknown event name → CustomEvent fallback | PASS | — |
| `add_event_node` | Duplicate override → error | PASS | — |
| `add_comment_node` | Basic comment box | PASS | — |
| `add_comment_node` | Auto-size from node_ids | PASS | GroupMovement mode |

---

## Blueprint — CustomEvent Duplicate Guard (2026-03-17)

| Action | Test | Result | Notes |
|--------|------|--------|-------|
| `add_node` (CustomEvent) | First creation → success | PASS | Returns K2Node_CustomEvent_0 with correct custom_name |
| `add_node` (CustomEvent) | Duplicate name → error | PASS | "A custom event named 'UniqueTestEvent' already exists in this Blueprint" |
| `add_event_node` | Duplicate CustomEvent name → error | PASS | Same shared check — both code paths guarded |
| `batch_execute` | Two ops same CustomEvent name — op 0 success, op 1 error | PASS | stop_on_error:false — first succeeds, second gets the dupe error |
| `add_node` (CustomEvent) | Two different names → both succeed | PASS | AlphaEvent + BetaEvent both created cleanly |

---

## Test History

| Date | Scope | Result | Notes |
|------|-------|--------|-------|
| 2026-03-22 | MonolithUI module full test pass (42/42) | **42/42 PASS** | All 8 action classes verified: Widget CRUD (7), Slot (3), Templates (8), Styling (6), Animation (5), Binding (4), Settings (5), Accessibility (4). |
| 2026-03-18 | Animation Wave 8-10 full test pass (33/33 + 4 retests) | **37/37 PASS** | 12 bugs found and fixed. Covers IKRig read/write, IK Retargeter, Control Rig read/write, ABP variables, ABP linked assets, ABP state machine writes (add_state_to_machine, add_transition, set_transition_rule). ABP experimental gate removed — all CVar checks stripped. |
| 2026-03-18 | Niagara module full test pass (37/37 + 8 retests) | **45/45 PASS** | 16 bugs found and fixed. Covers waves 2-6: summary/discovery, DI curve config, system management, dynamic inputs, event handlers, simulation stages, validate_system. |
| 2026-03-17 | Material module full test pass (44/44 + 11 retests) | **55/55 PASS** | 11 bugs found and fixed. Covers all wave 2-6 actions: auto_layout, duplicate_expression, move_expression, instance params, function graph, batch ops, import_texture, and all bug fix verifications. |
| 2026-03-17 | Blueprint module waves 2-7 full test pass (48/48 + 17 retests) | **65/65 PASS** | 21 bugs found and fixed. Covers batch_execute, discovery, bulk ops, scaffolding, inspection, advanced actions, and all bug fix verifications. |
| 2026-03-17 | Blueprint CustomEvent duplicate guard: add_node, add_event_node, batch_execute (5 tests) | **5/5 PASS** | Both add_node and add_event_node paths share the same guard. Batch handles partial failure cleanly. Test BP: /Game/Tests/Monolith/BP_Test_CustomEventGuard |
| 2026-03-16 | Blueprint Wave 5: scaffold_interface_implementation, add_timeline, add_event_node, add_comment_node (4 new actions) | **PENDING** | Implemented, needs build + test |
| 2026-03-15 | HLSL module creation: create_module_from_hlsl (CPU simple, CPU multi-type, GPU sim), create_function_from_hlsl, get_module_inputs + set_module_input_value on CustomHlsl, dot validation | **6/6 PASS** | 3 bugs fixed: numeric index lookup, input exposure fallback, dot validation. All prior actions still PASS. |
| 2026-03-14 | Niagara polish pass: set_system_property, set_static_switch_value, list_module_scripts, list_renderer_properties, get_system_property + DI prefix fix, curve readback fix, BP node alias fix | **7/7 PASS** | 213→218 actions. All prior actions still PASS. |
| 2026-03-13 | Infrastructure: registry param validation, Niagara aliases, material PostEditChange fixes, tools/list schemas | **6 fixes** | All 213 actions still PASS. Behavioral improvements — no regressions |
| 2026-03-13 | Niagara get_system_diagnostics + SimTarget/CalculateBoundsMode + 4 bug fixes | **8/8 PASS** | False DataMissing, add_emitter loading, create_system_from_spec save_path, create_system template — all fixed |
| 2026-03-13 | Bug fix verification | **3/3 PASS** | `add_component` name fix, `create_blueprint` crash guard, `set_module_input_value` data input fix. |
| 2026-03-13 | Blueprint full pass | **46/46 PASS** | 6→46 actions. `remove_local_variable` bug fixed. 3 deprecation fixes. All tested via MCP. |
| 2026-03-13 | Core params fix | PASS | Claude Code string-params bug fixed. `project_query("search")` now works from Claude Code. |
| 2026-03-11 | Niagara full pass | **41/41 PASS** | 12 bugs found+fixed. `set_emitter_property` confirmed working. |
| 2026-03-10 | Animation write actions | **14/14 PASS** | 5 bugs found+fixed. |
| 2026-03-10 | Material write actions | **11/11 PASS** | Wave 2: 14→25 material actions. |
| 2026-03-09 | All read actions | **36/36 PASS** | Full read coverage across all modules. |
| 2026-03-06 | Initial build | PASS | All 9 modules compile clean on UE 5.7. |
