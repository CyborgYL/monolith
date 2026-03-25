# Monolith — Pending Test Results

Last updated: 2026-03-25

---

## Material Function Suite (9 new actions + 1 extended)

Implemented 2026-03-25. Tested same day via MCP.

| # | Action | Status | Notes |
|---|--------|--------|-------|
| 1 | `export_function_graph` | PASS | Metadata, inputs (type/preview_value), outputs, nodes with parameter enrichment (parameter_name/group/sort_priority/default_value/dynamic_branch), connections, custom_hlsl_nodes. Tested with include_properties/include_positions both true and false. |
| 2 | `export_function_graph` (round-trip) | PENDING | Needs manual test — feed export JSON back into build_function_graph |
| 3 | `set_function_metadata` | PASS | Updated description, expose_to_library, library_categories. Verified via get_function_info. |
| 4 | `delete_function_expression` | PASS | Batch delete (comma-separated) works. not_found reported for bad names. Deleted count correct. |
| 5 | `update_material_function` | PASS | Recompile cascade triggered successfully. |
| 6 | `create_function_instance` | PASS | Created MFI with scalar + static_switch overrides. Verified via get_function_instance_info. Bug found and fixed: UpdateParameterSet() doesn't populate empty arrays — switched to manual GUID-based entry creation. |
| 7 | `set_function_instance_parameter` | PASS | Updated scalar from 2.5 to 5.0. Verified. Same GUID fix applied. |
| 8 | `get_function_instance_info` | PASS | All 11 param type arrays present. Parent/base chain correct. Inputs/outputs from parent. total_overrides count correct. |
| 9 | `layout_function_expressions` | PASS | Arranged successfully. |
| 10 | `rename_function_parameter_group` | PASS | Renamed "None" → "MyGroup". Verified both params show new group via export_function_graph. |
| 11 | `create_material_function` (type param) | PASS | MaterialLayer and MaterialLayerBlend both created with correct type. Invalid type returns clear error. |

**Bugs found and fixed during testing:**
- `create_function_instance` / `set_function_instance_parameter`: `UpdateParameterSet()` only syncs names on existing entries — doesn't populate empty arrays. Fixed to create entries manually with ExpressionGUIDs from base function expressions. Required adding `MaterialExpressionScalarParameter.h` and `MaterialExpressionVectorParameter.h` includes. Full UBT rebuild needed (header change).

---

## CyborgYL PR #8 Contributions (3 extended actions)

Merged from PR #8, tested 2026-03-25.

| # | Action | Status | Notes |
|---|--------|--------|-------|
| 12 | `get_all_expressions` (MF support) | PASS | Returns expressions with `asset_type: "MaterialFunction"`. |
| 13 | `get_expression_details` (MF support) | PASS | Full property dump on MaterialFunction expression. Group rename verified. |
| 14 | `set_expression_property` (MF support) | PENDING | Needs dedicated test with property change + verify. |
| 15-17 | Niagara variable queries | PENDING | get_emitter_variables, get_system_variables, get_particle_attributes |

---

## Editor Fixes

| # | Action | Status | Notes |
|---|--------|--------|-------|
| 18 | `delete_assets` (configurable prefixes) | PASS | No prefixes = deletes freely. With allowed_prefixes = rejects outside prefix. Partial delete correctly reports success:false with found/deleted/not_found counts. |
| 19 | `delete_function_expression` undo | PENDING | Needs manual Ctrl+Z test in editor. |
| 20 | `export_function_graph` class names | PASS | Confirmed "Multiply" not "MaterialExpressionMultiply" — RemoveFromStart working. |

---

## Code Review Fixes (/simplify)

| Fix | Status | Notes |
|-----|--------|-------|
| Dead Params.IsValid() check | FIXED | Removed in ExportFunctionGraph |
| ClassName.Mid(18) magic number | FIXED | RemoveFromStart("MaterialExpression") in both export functions |
| DeleteFunctionExpression transaction | FIXED | BeginTransaction/EndTransaction added |
| DeleteFunctionExpression N+1 scan | FIXED | TMap lookup instead of linear scan per name |
| delete_assets success reporting | FIXED | success reflects actual outcome, added found count |

---

## Summary

**Material Function Suite:** 10/11 PASS, 1 PENDING (round-trip)
**PR #8:** 2/6 PASS, 4 PENDING (1 material + 3 niagara)
**Editor:** 2/3 PASS, 1 PENDING (undo test)
**Code Review:** 5/5 FIXED

**Total: 14 PASS, 6 PENDING, 1 bug found and fixed**
