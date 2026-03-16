---
name: wave7_impl
description: Wave 7 Advanced Blueprint actions — promote_pin_to_variable, add_replicated_variable, DynamicCast node, soft reference shorthands
type: project
---

Wave 7 adds 2 new actions + 2 improvements (55 -> 57 total before any post-6 count reconciliation).

**Why:** Blueprint 47→67 plan, Wave 7 = Advanced actions.

**How to apply:** Reference when extending or debugging these actions.

## Actions added

### `promote_pin_to_variable` — NodeActions.h/.cpp
- Promotes a scalar pin on an existing node to a BP member variable
- Creates VariableGet (output pins) or VariableSet (input pins) and wires to original consumers
- Scalar only: rejects `EPinContainerType != None` with: "Container types (Array, Map, Set) are not yet supported by promote_pin_to_variable. Use add_variable + manual wiring instead."
- Sequence: find pin → validate (no exec/wildcard/container) → AddMemberVariable → MarkBlueprintAsStructurallyModified → create var node → rewire consumers → break original links
- Graph search: if GraphName empty, finds the graph by scanning for the node_id across UbergraphPages/FunctionGraphs/MacroGraphs
- Variable node positioned 200px left/right of source node
- Returns: `{variable_name, variable_type, getter_node_id OR setter_node_id, connections_made, graph}`

### `add_replicated_variable` — VariableActions.h/.cpp
- Adds a member variable with CPF_Net flag + ELifetimeCondition
- replication_condition maps: None, InitialOnly, OwnerOnly, SkipOwner, SimulatedOnly, AutonomousOnly, SimulatedOrPhysics, InitialOrOwner, Custom
- create_on_rep: creates `OnRep_<VarName>` function graph via CreateNewGraph + AddFunctionGraph<UClass>, then calls SetBlueprintVariableRepNotifyFunc to link
- Returns: `{variable_name, type, replicated: true, replication_condition, on_rep_function?}`
- Includes Net/UnrealNetwork.h for ELifetimeCondition

## Improvements

### DynamicCast node in `add_node`
- Added `cast`/`dynamic_cast` aliases → `DynamicCast` in the alias table
- New DynamicCast handler block after SpawnActorFromClass block
- Accepts `cast_class` as primary param; `actor_class` as deprecated fallback
- Tries bare name, then A-prefix, then U-prefix for class lookup
- Sets `UK2Node_DynamicCast::TargetType = CastClass`
- Include added: `K2Node_DynamicCast.h`

### Soft reference shorthands in `add_variable`
- `soft_object:ClassName` → `softobject:ClassName` (shorthand normalization)
- `soft_class:ClassName` → `softclass:ClassName`
- Applied BEFORE the existing alias normalizer block
- Also applied in `add_replicated_variable`
- `ParsePinTypeFromString` in MonolithBlueprintInternal.h now properly handles `softobject:` → `PC_SoftObject` and `softclass:` → `PC_SoftClass` (this was a pre-existing bug — these passed the error guard but fell through to bool default)

## batch_execute dispatch
- `promote_pin_to_variable` → `HandlePromotePinToVariable(SubParams)`
- `add_replicated_variable` → `FMonolithBlueprintVariableActions::HandleAddReplicatedVariable(SubParams)`
- Both added under "Wave 7 advanced ops" comment block

## Files modified
- `Source/MonolithBlueprint/Public/MonolithBlueprintNodeActions.h` — HandlePromotePinToVariable declaration
- `Source/MonolithBlueprint/Public/MonolithBlueprintVariableActions.h` — HandleAddReplicatedVariable declaration
- `Source/MonolithBlueprint/Private/MonolithBlueprintNodeActions.cpp` — K2Node_DynamicCast.h include; cast aliases; DynamicCast handler; cast_class param doc; promote_pin_to_variable registration + implementation; Wave 7 batch_execute dispatch; batch_execute description update
- `Source/MonolithBlueprint/Private/MonolithBlueprintVariableActions.cpp` — EdGraphSchema_K2.h + Net/UnrealNetwork.h includes; soft_object/soft_class shorthands; add_replicated_variable registration + implementation
- `Source/MonolithBlueprint/Private/MonolithBlueprintInternal.h` — ParsePinTypeFromString: added softobject: → PC_SoftObject and softclass: → PC_SoftClass handling

## Key gotchas
- `ParsePinTypeFromString` had a latent bug: `softobject:` and `softclass:` were in the bool-fallback bypass guard but not actually parsed → they'd silently create bools. Fixed in this wave by adding proper PC_SoftObject/PC_SoftClass branches.
- `promote_pin_to_variable` must call MarkBlueprintAsStructurallyModified BEFORE creating the variable node, so the skeleton class regenerates and the new var is available.
- `add_replicated_variable`'s `create_on_rep` path: if CreateNewGraph returns null (non-fatal), OnRepFunctionName is cleared and the response omits `on_rep_function`. The variable is still created successfully.
- DynamicCast: TargetType is the correct member name on UK2Node_DynamicCast (UClass*).
