#include "MonolithBlueprintGraphActions.h"
#include "MonolithBlueprintInternal.h"
#include "MonolithJsonUtils.h"
#include "MonolithParamSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditorLibrary.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Event.h"
#include "EdGraphSchema_K2.h"

// --- Registration ---

void FMonolithBlueprintGraphActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_function"),
		TEXT("Add a new function graph to a Blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleAddFunction),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Function name"))
			.Optional(TEXT("is_pure"), TEXT("bool"), TEXT("Mark as pure (no exec pins)"), TEXT("false"))
			.Optional(TEXT("is_const"), TEXT("bool"), TEXT("Mark as const"), TEXT("false"))
			.Optional(TEXT("is_static"), TEXT("bool"), TEXT("Mark as static"), TEXT("false"))
			.Optional(TEXT("call_in_editor"), TEXT("bool"), TEXT("Show 'Call In Editor' button"), TEXT("false"))
			.Optional(TEXT("category"), TEXT("string"), TEXT("Function category"))
			.Optional(TEXT("description"), TEXT("string"), TEXT("Function tooltip/description"))
			.Optional(TEXT("access"), TEXT("string"), TEXT("Access specifier: Public, Protected, or Private"), TEXT("Public"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("remove_function"),
		TEXT("Remove a function graph from a Blueprint by name"),
		FMonolithActionHandler::CreateStatic(&HandleRemoveFunction),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Function name to remove"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("rename_function"),
		TEXT("Rename an existing function graph in a Blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleRenameFunction),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("old_name"), TEXT("string"), TEXT("Current function name"))
			.Required(TEXT("new_name"), TEXT("string"), TEXT("New function name"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_macro"),
		TEXT("Add a new macro graph to a Blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleAddMacro),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Macro name"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("add_event_dispatcher"),
		TEXT("Add a new event dispatcher (multicast delegate) to a Blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleAddEventDispatcher),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Event dispatcher name"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("set_function_params"),
		TEXT("Add input/output parameters to a Blueprint function"),
		FMonolithActionHandler::CreateStatic(&HandleSetFunctionParams),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("function_name"), TEXT("string"), TEXT("Function graph name"))
			.Optional(TEXT("inputs"), TEXT("array"), TEXT("Array of {name, type} objects for inputs"))
			.Optional(TEXT("outputs"), TEXT("array"), TEXT("Array of {name, type} objects for outputs"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("implement_interface"),
		TEXT("Add an interface to a Blueprint's implemented interface list"),
		FMonolithActionHandler::CreateStatic(&HandleImplementInterface),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("interface_class"), TEXT("string"), TEXT("Interface class name (e.g. IMyInterface)"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("remove_interface"),
		TEXT("Remove an interface from a Blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleRemoveInterface),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("interface_class"), TEXT("string"), TEXT("Interface class name to remove"))
			.Optional(TEXT("preserve_functions"), TEXT("bool"), TEXT("Keep stub functions after removal"), TEXT("false"))
			.Build());

	Registry.RegisterAction(TEXT("blueprint"), TEXT("reparent_blueprint"),
		TEXT("Change the parent class of a Blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleReparentBlueprint),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("new_parent_class"), TEXT("string"), TEXT("New parent class name"))
			.Build());

	// ---- Wave 5 ----

	Registry.RegisterAction(TEXT("blueprint"), TEXT("scaffold_interface_implementation"),
		TEXT("Add an interface to a Blueprint AND create all stub function graphs in one call. Returns the interface name and list of created graphs. Much more useful than implement_interface alone — this one actually wires up the stubs."),
		FMonolithActionHandler::CreateStatic(&HandleScaffoldInterfaceImplementation),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"),       TEXT("string"), TEXT("Blueprint asset path"))
			.Required(TEXT("interface_class"),   TEXT("string"), TEXT("Interface class name (e.g. BPI_Interactable or IBpi_Interactable)"))
			.Build());
}

// --- add_function ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleAddFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString FuncName = Params->GetStringField(TEXT("name"));
	if (FuncName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: name"));
	}

	// Check for name collision
	for (const UEdGraph* Existing : BP->FunctionGraphs)
	{
		if (Existing && Existing->GetName() == FuncName)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Function already exists: %s"), *FuncName));
		}
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, FName(*FuncName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (!NewGraph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to create function graph: %s"), *FuncName));
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, /*bIsUserCreated=*/true, nullptr);

	// Find the entry node to set metadata and flags
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}

	if (EntryNode)
	{
		uint32 ExtraFlags = EntryNode->GetFunctionFlags();

		bool bIsPure = false;
		bool bIsConst = false;
		bool bIsStatic = false;
		bool bCallInEditor = false;
		Params->TryGetBoolField(TEXT("is_pure"), bIsPure);
		Params->TryGetBoolField(TEXT("is_const"), bIsConst);
		Params->TryGetBoolField(TEXT("is_static"), bIsStatic);
		Params->TryGetBoolField(TEXT("call_in_editor"), bCallInEditor);

		if (bIsPure)        ExtraFlags |= FUNC_BlueprintPure;
		if (bIsConst)       ExtraFlags |= FUNC_Const;
		if (bIsStatic)      ExtraFlags |= FUNC_Static;

		// Access specifier
		FString Access;
		Params->TryGetStringField(TEXT("access"), Access);
		ExtraFlags &= ~(FUNC_Protected | FUNC_Private); // clear existing
		if (Access == TEXT("Protected"))      ExtraFlags |= FUNC_Protected;
		else if (Access == TEXT("Private"))   ExtraFlags |= FUNC_Private;

		EntryNode->SetExtraFlags(ExtraFlags);
		EntryNode->MetaData.bCallInEditor = bCallInEditor;

		FString Category;
		Params->TryGetStringField(TEXT("category"), Category);
		if (!Category.IsEmpty())
		{
			EntryNode->MetaData.Category = FText::FromString(Category);
		}

		FString Description;
		Params->TryGetStringField(TEXT("description"), Description);
		if (!Description.IsEmpty())
		{
			EntryNode->MetaData.ToolTip = FText::FromString(Description);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph_name"), NewGraph->GetName());
	Root->SetNumberField(TEXT("node_count"), NewGraph->Nodes.Num());
	return FMonolithActionResult::Success(Root);
}

// --- remove_function ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleRemoveFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString FuncName = Params->GetStringField(TEXT("name"));
	if (FuncName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: name"));
	}

	// Only search function graphs — not the event graph or macros
	UEdGraph* Graph = nullptr;
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G && G->GetName() == FuncName)
		{
			Graph = G;
			break;
		}
	}

	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Function not found: %s"), *FuncName));
	}

	FBlueprintEditorUtils::RemoveGraph(BP, Graph, EGraphRemoveFlags::Recompile);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("removed_function"), FuncName);
	return FMonolithActionResult::Success(Root);
}

// --- rename_function ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleRenameFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString OldName = Params->GetStringField(TEXT("old_name"));
	FString NewName = Params->GetStringField(TEXT("new_name"));

	if (OldName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: old_name"));
	}
	if (NewName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: new_name"));
	}

	UEdGraph* Graph = MonolithBlueprintInternal::FindGraphByName(BP, OldName);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Function not found: %s"), *OldName));
	}

	// Ensure we're only renaming function graphs, not event graphs or macros
	if (!BP->FunctionGraphs.Contains(Graph))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Graph '%s' is not a function graph (cannot rename event graphs or macros this way)"), *OldName));
	}

	// Check for name collision
	for (const UEdGraph* Existing : BP->FunctionGraphs)
	{
		if (Existing && Existing != Graph && Existing->GetName() == NewName)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("A function named '%s' already exists"), *NewName));
		}
	}

	FBlueprintEditorUtils::RenameGraph(Graph, NewName);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("old_name"), OldName);
	Root->SetStringField(TEXT("new_name"), Graph->GetName());
	return FMonolithActionResult::Success(Root);
}

// --- add_macro ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleAddMacro(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString MacroName = Params->GetStringField(TEXT("name"));
	if (MacroName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: name"));
	}

	// Check for name collision
	for (const UEdGraph* Existing : BP->MacroGraphs)
	{
		if (Existing && Existing->GetName() == MacroName)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Macro already exists: %s"), *MacroName));
		}
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, FName(*MacroName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (!NewGraph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to create macro graph: %s"), *MacroName));
	}

	FBlueprintEditorUtils::AddMacroGraph(BP, NewGraph, /*bIsUserCreated=*/true, nullptr);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph_name"), NewGraph->GetName());
	Root->SetNumberField(TEXT("node_count"), NewGraph->Nodes.Num());
	return FMonolithActionResult::Success(Root);
}

// --- add_event_dispatcher ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleAddEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString DispatcherName = Params->GetStringField(TEXT("name"));
	if (DispatcherName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: name"));
	}

	// Generate a unique name for the delegate signature graph
	FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(BP, DispatcherName);

	// Check for name collision against the display name (without _Signature suffix)
	for (const UEdGraph* Existing : BP->DelegateSignatureGraphs)
	{
		if (!Existing) continue;
		FString ExistingDisplay = Existing->GetName();
		if (ExistingDisplay.EndsWith(TEXT("_Signature")))
		{
			ExistingDisplay.LeftChopInline(10, EAllowShrinking::No);
		}
		if (ExistingDisplay == DispatcherName)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Event dispatcher already exists: %s"), *DispatcherName));
		}
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, UniqueName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (!NewGraph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to create delegate signature graph: %s"), *DispatcherName));
	}

	BP->DelegateSignatureGraphs.Add(NewGraph);
	NewGraph->bEditable = false;

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->CreateDefaultNodesForGraph(*NewGraph);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	// Compute display name (strip _Signature if UE added it)
	FString DisplayName = NewGraph->GetName();
	if (DisplayName.EndsWith(TEXT("_Signature")))
	{
		DisplayName.LeftChopInline(10, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("dispatcher_name"), DisplayName);
	Root->SetStringField(TEXT("graph_name"), NewGraph->GetName());
	return FMonolithActionResult::Success(Root);
}

// --- set_function_params ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleSetFunctionParams(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString FuncName = Params->GetStringField(TEXT("function_name"));
	if (FuncName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: function_name"));
	}

	// Only function graphs can have params set this way
	UEdGraph* Graph = nullptr;
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G && G->GetName() == FuncName)
		{
			Graph = G;
			break;
		}
	}
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Function not found: %s"), *FuncName));
	}

	// Find entry and result nodes
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!EntryNode) EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (!ResultNode) ResultNode = Cast<UK2Node_FunctionResult>(Node);
		if (EntryNode && ResultNode) break;
	}

	if (!EntryNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("No FunctionEntry node found in: %s"), *FuncName));
	}

	int32 InputsAdded = 0;
	int32 OutputsAdded = 0;

	// Process inputs — add as user-defined pins on the entry node
	const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("inputs"), InputsArray) && InputsArray)
	{
		for (const TSharedPtr<FJsonValue>& InputVal : *InputsArray)
		{
			const TSharedPtr<FJsonObject>* InputObj = nullptr;
			if (!InputVal->TryGetObject(InputObj) || !InputObj) continue;

			FString PinName, TypeStr;
			(*InputObj)->TryGetStringField(TEXT("name"), PinName);
			(*InputObj)->TryGetStringField(TEXT("type"), TypeStr);

			if (PinName.IsEmpty() || TypeStr.IsEmpty()) continue;

			FEdGraphPinType PinType = MonolithBlueprintInternal::ParsePinTypeFromString(TypeStr);
			EntryNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output);
			++InputsAdded;
		}
	}

	// Process outputs — add as user-defined pins on the result node
	const TArray<TSharedPtr<FJsonValue>>* OutputsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("outputs"), OutputsArray) && OutputsArray)
	{
		if (!ResultNode)
		{
			// Create a result node if one doesn't exist
			FGraphNodeCreator<UK2Node_FunctionResult> Creator(*Graph);
			ResultNode = Creator.CreateNode();
			ResultNode->NodePosX = EntryNode ? EntryNode->NodePosX + 400 : 0;
			ResultNode->NodePosY = EntryNode ? EntryNode->NodePosY : 0;
			Creator.Finalize();
		}

		for (const TSharedPtr<FJsonValue>& OutputVal : *OutputsArray)
		{
			const TSharedPtr<FJsonObject>* OutputObj = nullptr;
			if (!OutputVal->TryGetObject(OutputObj) || !OutputObj) continue;

			FString PinName, TypeStr;
			(*OutputObj)->TryGetStringField(TEXT("name"), PinName);
			(*OutputObj)->TryGetStringField(TEXT("type"), TypeStr);

			if (PinName.IsEmpty() || TypeStr.IsEmpty()) continue;

			FEdGraphPinType PinType = MonolithBlueprintInternal::ParsePinTypeFromString(TypeStr);
			ResultNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Input);
			++OutputsAdded;
		}
	}

	if (InputsAdded == 0 && OutputsAdded == 0)
	{
		return FMonolithActionResult::Error(TEXT("No valid inputs or outputs provided"));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("function_name"), FuncName);
	Root->SetNumberField(TEXT("inputs_added"), InputsAdded);
	Root->SetNumberField(TEXT("outputs_added"), OutputsAdded);
	return FMonolithActionResult::Success(Root);
}

// --- implement_interface ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleImplementInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString InterfaceClassName = Params->GetStringField(TEXT("interface_class"));
	if (InterfaceClassName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: interface_class"));
	}

	// Verify the class exists before attempting to add it
	UClass* InterfaceClass = FindFirstObject<UClass>(*InterfaceClassName, EFindFirstObjectOptions::NativeFirst);
	if (!InterfaceClass)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Interface class not found: %s"), *InterfaceClassName));
	}

	if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Class '%s' is not an interface"), *InterfaceClassName));
	}

	// Check if already implemented
	for (const FBPInterfaceDescription& Existing : BP->ImplementedInterfaces)
	{
		if (Existing.Interface == InterfaceClass)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Interface already implemented: %s"), *InterfaceClassName));
		}
	}

	const bool bAdded = FBlueprintEditorUtils::ImplementNewInterface(BP, InterfaceClass->GetClassPathName());
	if (!bAdded)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to implement interface: %s"), *InterfaceClassName));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("interface_class"), InterfaceClassName);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	return FMonolithActionResult::Success(Root);
}

// --- remove_interface ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleRemoveInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString InterfaceClassName = Params->GetStringField(TEXT("interface_class"));
	if (InterfaceClassName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: interface_class"));
	}

	// Verify the interface is actually implemented
	UClass* InterfaceClass = nullptr;
	for (const FBPInterfaceDescription& Existing : BP->ImplementedInterfaces)
	{
		if (Existing.Interface && Existing.Interface->GetName() == InterfaceClassName)
		{
			InterfaceClass = Existing.Interface;
			break;
		}
	}

	if (!InterfaceClass)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Interface not implemented by this Blueprint: %s"), *InterfaceClassName));
	}

	bool bPreserveFunctions = false;
	Params->TryGetBoolField(TEXT("preserve_functions"), bPreserveFunctions);

	FBlueprintEditorUtils::RemoveInterface(BP, InterfaceClass->GetClassPathName(), bPreserveFunctions);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("interface_class"), InterfaceClassName);
	Root->SetBoolField(TEXT("functions_preserved"), bPreserveFunctions);
	return FMonolithActionResult::Success(Root);
}

// --- scaffold_interface_implementation ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleScaffoldInterfaceImplementation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString InterfaceClassName = Params->GetStringField(TEXT("interface_class"));
	if (InterfaceClassName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: interface_class"));
	}

	// Resolve the interface class — try as-is, then strip leading 'I', then add 'U' prefix
	UClass* InterfaceClass = FindFirstObject<UClass>(*InterfaceClassName, EFindFirstObjectOptions::NativeFirst);
	if (!InterfaceClass && InterfaceClassName.StartsWith(TEXT("I")))
	{
		// Blueprint interfaces typically have U prefix in the class system (e.g. IBpi_Interactable -> UBpi_Interactable)
		InterfaceClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *InterfaceClassName.Mid(1)), EFindFirstObjectOptions::NativeFirst);
	}
	if (!InterfaceClass)
	{
		InterfaceClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *InterfaceClassName), EFindFirstObjectOptions::NativeFirst);
	}
	if (!InterfaceClass)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Interface class not found: %s"), *InterfaceClassName));
	}

	if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Class '%s' is not an interface"), *InterfaceClassName));
	}

	// Check if already implemented
	bool bAlreadyImplemented = false;
	for (const FBPInterfaceDescription& Existing : BP->ImplementedInterfaces)
	{
		if (Existing.Interface == InterfaceClass)
		{
			bAlreadyImplemented = true;
			break;
		}
	}

	// Snapshot function graph names before implementing so we can detect which were newly created
	TSet<FName> GraphsBefore;
	for (const UEdGraph* G : BP->FunctionGraphs)
	{
		if (G) GraphsBefore.Add(G->GetFName());
	}
	TSet<FName> UbergraphsBefore;
	for (const UEdGraph* G : BP->UbergraphPages)
	{
		if (G) UbergraphsBefore.Add(G->GetFName());
	}

	if (!bAlreadyImplemented)
	{
		// ImplementNewInterface requires FTopLevelAssetPath (not the deprecated FName overload)
		const bool bAdded = FBlueprintEditorUtils::ImplementNewInterface(BP, InterfaceClass->GetClassPathName());
		if (!bAdded)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("ImplementNewInterface failed for: %s"), *InterfaceClassName));
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}

	// Collect newly created graphs — compare against pre-implementation snapshots
	// Functions with return values get function graphs; void functions become event nodes in the ubergraph
	TArray<TSharedPtr<FJsonValue>> FunctionsCreated;

	for (const UEdGraph* G : BP->FunctionGraphs)
	{
		if (!G) continue;
		if (GraphsBefore.Contains(G->GetFName())) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), G->GetName());
		Entry->SetStringField(TEXT("graph_name"), G->GetName());
		Entry->SetBoolField(TEXT("is_event"), false);
		FunctionsCreated.Add(MakeShared<FJsonValueObject>(Entry));
	}

	// Also detect new event nodes added to ubergraph pages (void interface functions)
	for (const UEdGraph* G : BP->UbergraphPages)
	{
		if (!G) continue;
		for (const UEdGraphNode* Node : G->Nodes)
		{
			if (const UK2Node_Event* EvNode = Cast<UK2Node_Event>(Node))
			{
				// Interface events added by ImplementNewInterface will be overrides
				if (EvNode->bOverrideFunction)
				{
					// Check that this event's function is from the interface we just added
					if (EvNode->EventReference.GetMemberParentClass() == InterfaceClass ||
						InterfaceClass->FindFunctionByName(EvNode->EventReference.GetMemberName()))
					{
						// Was this node in the ubergraph before? We don't have per-node snapshot,
						// so report all override events belonging to this interface.
						// When already_implemented=true, we still list them so the caller knows what's there.
						TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
						Entry->SetStringField(TEXT("name"), EvNode->EventReference.GetMemberName().ToString());
						Entry->SetStringField(TEXT("graph_name"), G->GetName());
						Entry->SetBoolField(TEXT("is_event"), true);
						FunctionsCreated.Add(MakeShared<FJsonValueObject>(Entry));
					}
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("interface_name"), InterfaceClass->GetName());
	Root->SetArrayField(TEXT("functions_created"), FunctionsCreated);
	Root->SetBoolField(TEXT("already_implemented"), bAlreadyImplemented);
	return FMonolithActionResult::Success(Root);
}

// --- reparent_blueprint ---

FMonolithActionResult FMonolithBlueprintGraphActions::HandleReparentBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBlueprint* BP = MonolithBlueprintInternal::LoadBlueprintFromParams(Params, AssetPath);
	if (!BP)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString ClassName = Params->GetStringField(TEXT("new_parent_class"));
	if (ClassName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: new_parent_class"));
	}

	UClass* NewParent = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	if (!NewParent)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Parent class not found: %s"), *ClassName));
	}

	if (NewParent->HasAnyClassFlags(CLASS_Interface))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Cannot reparent to an interface class: %s"), *ClassName));
	}

	FString OldParent = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None");

	UBlueprintEditorLibrary::ReparentBlueprint(BP, NewParent);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("old_parent_class"), OldParent);
	Root->SetStringField(TEXT("new_parent_class"), NewParent->GetName());
	return FMonolithActionResult::Success(Root);
}
