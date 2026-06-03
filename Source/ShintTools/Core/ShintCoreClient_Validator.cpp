// Copyright ShintTools. All Rights Reserved.
//
// Code Validator implementations split out of ShintCoreClient.cpp.
// The whole class still lives in ShintCoreClient.h — UBT compiles every
// .cpp in the module, so member functions can be physically scattered
// without touching headers or callsites.

#include "ShintCoreClient.h"
#include "ShintTools/ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "GameFramework/Actor.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — single file
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateCode(
	const FString& AbsFilePath, const FString& Content,
	const FString& Engine, FOnShintValidateComplete OnComplete)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("file_path"), AbsFilePath);
	Body->SetStringField(TEXT("content"),   Content);
	Body->SetStringField(TEXT("engine"),    Engine);
	// api_key drives resolve_tier on the core; without it every paid
	// user was bucketed as "free" with the limit_applied flag tripped.
	// See ValidateBlueprints for the matching comment.
	Body->SetStringField(TEXT("api_key"),   Config.ApiKeyMongo);

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/code"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			OnComplete.ExecuteIfBound(ParseValidateResponse(Raw));
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — full project
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateProject(
	const FString& SourceDir, FOnShintValidateComplete OnComplete)
{
	const double BenchStart = FPlatformTime::Seconds();
	TArray<FString> AbsFiles;
	CollectSourceFiles(SourceDir, AbsFiles);

	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Scanning %d source files from %s"), AbsFiles.Num(), *SourceDir);

	TMap<FString, FString> FilenameLookup;
	TArray<TSharedPtr<FJsonValue>> FilesArr;

	for (const FString& Abs : AbsFiles)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Abs))
			continue;

		const FString Filename = FPaths::GetCleanFilename(Abs);
		const FString Ext = FPaths::GetExtension(Abs).ToLower();
		const FString TypeStr = (Ext == TEXT("h") || Ext == TEXT("hpp")) ? TEXT("header") : TEXT("cpp");

		TArray<FString> Lines;
		const int32 LineCount = Content.ParseIntoArray(Lines, TEXT("\n"), false);

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("name"), Filename);
		FO->SetStringField(TEXT("path"), Abs);
		FO->SetStringField(TEXT("type"), TypeStr);
		FO->SetStringField(TEXT("content"), Content);
		FO->SetNumberField(TEXT("lines_count"), LineCount);

		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
		FilenameLookup.Add(Filename, Abs);
	}

	if (FilesArr.IsEmpty())
	{
		FShintValidateResult Empty;
		Empty.bSuccess = true;
		OnComplete.ExecuteIfBound(Empty);
		return;
	}

	// project_id removed in 1.7.11 — the dashboard uses the per-project API
	// key for identification; the local core uses project_name + api_key.
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("engine"), TEXT("unreal"));
	Body->SetStringField(TEXT("api_key"), Config.ApiKeyMongo);
	Body->SetArrayField(TEXT("files"), FilesArr);

	TArray<FString> CapturedFiles = AbsFiles;

	const FString BodyStr = SerializeJson(Body);
	UE_LOG(LogShintTools, Log, TEXT("Validate Project JSON size: %d chars"), BodyStr.Len());

	const int32 LogChunkSize = 1000;
	for (int32 i = 0; i < BodyStr.Len(); i += LogChunkSize)
	{
		UE_LOG(LogShintTools, Verbose, TEXT("%s"), *BodyStr.Mid(i, LogChunkSize));
	}

	SendRequest(
		Config.GetBaseUrl() + TEXT("/validate/project"),
		EShintHttpMethod::POST,
		BodyStr,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete, CapturedFiles, FilenameLookup, BenchStart](const FShintRequestResult& Raw) mutable
			{
				FShintValidateResult Result = FShintCoreClient::ParseValidateResponse(Raw);
				Result.ScannedFilePaths = CapturedFiles;

				for (FShintCodeIssue& Issue : Result.Issues)
				{
					if (!Issue.FilePath.IsEmpty() && !FPaths::FileExists(Issue.FilePath))
					{
						const FString Fn = FPaths::GetCleanFilename(Issue.FilePath);
						if (const FString* Found = FilenameLookup.Find(Fn))
						{
							Issue.FilePath = *Found;
						}
					}
				}

				const double BenchEnd = FPlatformTime::Seconds();
				UE_LOG(LogShintTools, Log,
					TEXT("[BENCH] ValidateProject: %.2f s, %d files, %d issues"),
					BenchEnd - BenchStart, CapturedFiles.Num(), Result.Issues.Num());

				OnComplete.ExecuteIfBound(Result);
			}
		)
	);
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — blueprints
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateBlueprints(
	const FString& ContentDir, FOnShintValidateComplete OnComplete)
{
	const double BenchStart = FPlatformTime::Seconds();
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths   = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> BlueprintAssets;
	AR.GetAssets(Filter, BlueprintAssets);

	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Found %d project blueprints under /Game/"), BlueprintAssets.Num());

	TArray<TSharedPtr<FJsonValue>> FilesArr;

	TArray<TSharedPtr<FJsonValue>> EmptyArr;
	auto MakeEmptyStats = []() -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("total_nodes"),        0);
		S->SetNumberField(TEXT("cast_nodes"),         0);
		S->SetBoolField  (TEXT("tick_enabled"),       false);
		S->SetNumberField(TEXT("disconnected_nodes"), 0);
		S->SetBoolField  (TEXT("has_begin_play_super"), false);
		S->SetBoolField  (TEXT("has_end_play_super"),   false);
		return S;
	};

	int32 LoadedCount = 0;
	int32 SkippedCount = 0;

	for (const FAssetData& AssetData : BlueprintAssets)
	{
		const FString BPName    = AssetData.AssetName.ToString();
		const FString BPPackage = AssetData.PackageName.ToString();

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("name"), BPName);
		FO->SetStringField(TEXT("path"), BPPackage);
		FO->SetStringField(TEXT("type"), TEXT("blueprint"));

		UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
		if (!BP)
		{
			FO->SetArrayField (TEXT("graphs"),    EmptyArr);
			FO->SetArrayField (TEXT("variables"), EmptyArr);
			FO->SetArrayField (TEXT("functions"), EmptyArr);
			FO->SetObjectField(TEXT("stats"),     MakeEmptyStats());
			FilesArr.Add(MakeShared<FJsonValueObject>(FO));
			++SkippedCount;
			continue;
		}
		++LoadedCount;

		TArray<UEdGraph*> AllGraphs;
		AllGraphs.Append(BP->UbergraphPages);
		AllGraphs.Append(BP->FunctionGraphs);

		TSet<FName> UsedVarNames;

		int32 TotalNodes        = 0;
		int32 TotalCastNodes    = 0;
		int32 DisconnectedNodes = 0;
		bool  bHasBeginPlaySuper = false;
		bool  bHasEndPlaySuper   = false;

		bool bTickEnabled = false;
		if (UBlueprintGeneratedClass* GenClass = Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))
		{
			if (AActor* CDO = Cast<AActor>(GenClass->GetDefaultObject(false)))
				bTickEnabled = CDO->PrimaryActorTick.bCanEverTick;
		}

		TArray<TSharedPtr<FJsonValue>> GraphsArr;
		TArray<TSharedPtr<FJsonValue>> FunctionsArr;

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;

			const int32 GraphNodeCount = Graph->Nodes.Num();
			TotalNodes += GraphNodeCount;

			int32 GraphCastCount    = 0;
			int32 GraphDisconnected = 0;
			TMap<FString, int32> NodeTypeCounts;

			const bool bIsFunctionGraph = BP->FunctionGraphs.Contains(Graph);
			int32 FuncComplexity = 1;
			bool  bFuncIsPublic  = true;
			bool  bFuncHasTooltip = false;

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;

				const FString ClassName = Node->GetClass()->GetName();

				FString NodeType = TEXT("Other");

				if (ClassName.Contains(TEXT("DynamicCast")))
				{
					NodeType = TEXT("CastTo");
					++GraphCastCount;
				}
				else if (ClassName.Contains(TEXT("K2Node_Event")) || ClassName.Contains(TEXT("K2Node_CustomEvent")))
				{
					const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
					if (Title.Contains(TEXT("Tick")))        NodeType = TEXT("EventTick");
					else if (Title.Contains(TEXT("BeginPlay"))) NodeType = TEXT("EventBeginPlay");
					else if (Title.Contains(TEXT("EndPlay")))   NodeType = TEXT("EventEndPlay");
					else NodeType = TEXT("Event");
				}
				else if (ClassName.Contains(TEXT("CallParentFunction")))
				{
					NodeType = TEXT("CallParent");
					const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
					if (Title.Contains(TEXT("BeginPlay"))) bHasBeginPlaySuper = true;
					if (Title.Contains(TEXT("EndPlay")))   bHasEndPlaySuper   = true;
				}
				else if (ClassName.Contains(TEXT("Delay")))
				{
					NodeType = TEXT("Delay");
				}
				else if (ClassName.Contains(TEXT("IfThenElse")) || ClassName.Contains(TEXT("Switch")))
				{
					NodeType = TEXT("Branch");
					++FuncComplexity;
				}

				if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
					UsedVarNames.Add(VarGet->VariableReference.GetMemberName());
				else if (UK2Node_VariableSet* VarSet = Cast<UK2Node_VariableSet>(Node))
					UsedVarNames.Add(VarSet->VariableReference.GetMemberName());

				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					bFuncHasTooltip = !Entry->MetaData.ToolTip.IsEmpty();
					bFuncIsPublic   = (Entry->GetFunctionFlags() & FUNC_Public) != 0;
				}

				// Event / function-entry / function-result nodes are graph
				// roots: an empty BeginPlay or a function with no body has
				// every pin unconnected by definition and is NOT a
				// "disconnected orphan node" — flagging them drowned BPM002
				// in false positives.
				const bool bIsGraphRoot =
					ClassName.Contains(TEXT("K2Node_Event")) ||
					ClassName.Contains(TEXT("K2Node_CustomEvent")) ||
					ClassName.Contains(TEXT("K2Node_FunctionEntry")) ||
					ClassName.Contains(TEXT("K2Node_FunctionResult")) ||
					ClassName.Contains(TEXT("K2Node_Tunnel")) ||
					ClassName.Contains(TEXT("Comment"));

				if (!bIsGraphRoot)
				{
					bool bHasAnyConnection = false;
					for (const UEdGraphPin* Pin : Node->Pins)
					{
						if (Pin && Pin->LinkedTo.Num() > 0)
						{
							bHasAnyConnection = true;
							break;
						}
					}
					if (!bHasAnyConnection && Node->Pins.Num() > 0)
						++GraphDisconnected;
				}

				NodeTypeCounts.FindOrAdd(NodeType)++;
			}

			TotalCastNodes    += GraphCastCount;
			DisconnectedNodes += GraphDisconnected;

			TArray<TSharedPtr<FJsonValue>> NodesArr;
			for (auto& Pair : NodeTypeCounts)
			{
				TSharedRef<FJsonObject> NObj = MakeShared<FJsonObject>();
				NObj->SetStringField(TEXT("type"),  Pair.Key);
				NObj->SetNumberField(TEXT("count"), Pair.Value);
				NodesArr.Add(MakeShared<FJsonValueObject>(NObj));
			}

			TSharedRef<FJsonObject> GraphObj = MakeShared<FJsonObject>();
			GraphObj->SetStringField(TEXT("name"),        Graph->GetName());
			GraphObj->SetNumberField(TEXT("nodes_count"), GraphNodeCount);
			GraphObj->SetArrayField (TEXT("nodes"),       NodesArr);
			GraphsArr.Add(MakeShared<FJsonValueObject>(GraphObj));

			if (bIsFunctionGraph)
			{
				TSharedRef<FJsonObject> FuncObj = MakeShared<FJsonObject>();
				FuncObj->SetStringField(TEXT("name"),        Graph->GetName());
				FuncObj->SetNumberField(TEXT("complexity"),  FuncComplexity);
				FuncObj->SetBoolField  (TEXT("is_public"),   bFuncIsPublic);
				FuncObj->SetBoolField  (TEXT("has_tooltip"), bFuncHasTooltip);
				FunctionsArr.Add(MakeShared<FJsonValueObject>(FuncObj));
			}
		}

		TArray<TSharedPtr<FJsonValue>> VariablesArr;
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			TSharedRef<FJsonObject> VObj = MakeShared<FJsonObject>();
			VObj->SetStringField(TEXT("name"),     Var.VarName.ToString());
			VObj->SetStringField(TEXT("type"),     Var.VarType.PinCategory.ToString());
			VObj->SetBoolField  (TEXT("used"),     UsedVarNames.Contains(Var.VarName));
			VObj->SetBoolField  (TEXT("is_public"),
				!(Var.PropertyFlags & CPF_DisableEditOnInstance));
			VObj->SetStringField(TEXT("category"), Var.Category.ToString());
			VariablesArr.Add(MakeShared<FJsonValueObject>(VObj));
		}

		TSharedRef<FJsonObject> StatsObj = MakeShared<FJsonObject>();
		StatsObj->SetNumberField(TEXT("total_nodes"),          TotalNodes);
		StatsObj->SetNumberField(TEXT("cast_nodes"),           TotalCastNodes);
		StatsObj->SetBoolField  (TEXT("tick_enabled"),         bTickEnabled);
		StatsObj->SetNumberField(TEXT("disconnected_nodes"),   DisconnectedNodes);
		StatsObj->SetBoolField  (TEXT("has_begin_play_super"), bHasBeginPlaySuper);
		StatsObj->SetBoolField  (TEXT("has_end_play_super"),   bHasEndPlaySuper);

		FO->SetArrayField (TEXT("graphs"),    GraphsArr);
		FO->SetArrayField (TEXT("variables"), VariablesArr);
		FO->SetArrayField (TEXT("functions"), FunctionsArr);
		FO->SetObjectField(TEXT("stats"),     StatsObj);
		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
	}

	UE_LOG(LogShintTools, Log,
		TEXT("ShintCoreClient: %d/%d blueprints loaded (%d skipped), sending to /validate/blueprints"),
		LoadedCount, BlueprintAssets.Num(), SkippedCount);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
	Body->SetStringField(TEXT("api_key"),      Config.ApiKeyMongo);
	Body->SetArrayField (TEXT("files"),        FilesArr);

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/blueprints"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete, BenchStart, LoadedCount](const FShintRequestResult& Raw) mutable {
			FShintValidateResult R = FShintCoreClient::ParseValidateResponse(Raw);
			UE_LOG(LogShintTools, Log,
				TEXT("[BENCH] ValidateBlueprints: %.2f s, %d BPs loaded, %d issues"),
				FPlatformTime::Seconds() - BenchStart, LoadedCount, R.Issues.Num());
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — apply fixes
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ApplyCodeFixes(
	const TArray<FShintCodeIssue>& AcceptedIssues, FOnShintFixComplete OnComplete)
{
	const double BenchFixStart = FPlatformTime::Seconds();
	TArray<FShintCodeIssue> TreeSitterIssues;
	TArray<FShintCodeIssue> LocalIssues;
	TArray<const FShintCodeIssue*> BPIssues;
	int32 SkippedNoFix = 0;

	for (const FShintCodeIssue& Issue : AcceptedIssues)
	{
		if (Issue.FilePath.IsEmpty()) continue;
		if (Issue.FilePath.StartsWith(TEXT("/Game/")) || Issue.FilePath.StartsWith(TEXT("/Engine/")))
		{
			BPIssues.Add(&Issue);
			continue;
		}
		if (!Issue.FileContent.IsEmpty())
		{
			TreeSitterIssues.Add(Issue);
		}
		else if (!Issue.FixSuggestion.IsEmpty())
		{
			LocalIssues.Add(Issue);
		}
		else
		{
			UE_LOG(LogShintTools, Warning,
				TEXT("ApplyFix: No content or fix_suggestion for [%s] line %d in %s — skipping"),
				*Issue.RuleId, Issue.Line, *Issue.FilePath);
			++SkippedNoFix;
		}
	}

	// UE5 LoadObject needs the full object path "/Game/Pkg/Asset.Asset", but
	// the server returns only the package path "/Game/Pkg/Asset".
	auto MakeBPPath = [](const FString& Pkg) -> FString
	{
		if (Pkg.IsEmpty() || Pkg.Contains(TEXT("."))) return Pkg;
		return Pkg + TEXT(".") + FPaths::GetBaseFilename(Pkg);
	};

	// E-005: ControlRigBlueprint does not use standard UEdGraphPin wiring, so
	// BPM001/BPM002 end up corrupting the rig graph. Detect via class hierarchy
	// to avoid a hard dep on the ControlRig module.
	auto IsControlRigBP = [](UBlueprint* BP) -> bool
	{
		if (!BP) return false;
		for (UClass* Cls = BP->GetClass(); Cls; Cls = Cls->GetSuperClass())
		{
			if (Cls->GetName().Contains(TEXT("ControlRig"))) return true;
		}
		if (BP->ParentClass)
		{
			for (UClass* Cls = BP->ParentClass; Cls; Cls = Cls->GetSuperClass())
			{
				if (Cls->GetName().Contains(TEXT("ControlRig"))) return true;
			}
		}
		return false;
	};

	int32 BPApplied = 0;
	int32 BPSkipped = 0;
	for (const FShintCodeIssue* Issue : BPIssues)
	{
		if (Issue->RuleId == TEXT("BPP001"))
		{
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *MakeBPPath(Issue->FilePath));
			if (BP && BP->GeneratedClass)
			{
				AActor* CDO = Cast<AActor>(BP->GeneratedClass->GetDefaultObject(true));
				if (CDO)
				{
					CDO->PrimaryActorTick.bCanEverTick        = false;
					CDO->PrimaryActorTick.bStartWithTickEnabled = false;
					(void)BP->MarkPackageDirty();
					UE_LOG(LogShintTools, Log,
						TEXT("ApplyFix: [BPP001] Disabled tick on '%s'"), *Issue->FilePath);
					++BPApplied;
					continue;
				}
			}
			UE_LOG(LogShintTools, Warning,
				TEXT("ApplyFix: [BPP001] Could not load BP '%s'"), *Issue->FilePath);
			++BPSkipped;
		}
		else if (Issue->RuleId == TEXT("BPM001"))
		{
			FString VarName;
			const FString& Msg = Issue->Message;
			int32 Q1 = INDEX_NONE, Q2 = INDEX_NONE;
			Msg.FindChar(TCHAR('\''), Q1);
			if (Q1 != INDEX_NONE)
				Q2 = Msg.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q1 + 1);

			if (Q1 != INDEX_NONE && Q2 != INDEX_NONE && Q2 > Q1)
				VarName = Msg.Mid(Q1 + 1, Q2 - Q1 - 1);

			if (!VarName.IsEmpty())
			{
				if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *MakeBPPath(Issue->FilePath)))
				{
					if (IsControlRigBP(BP))
					{
						UE_LOG(LogShintTools, Log,
							TEXT("ApplyFix: [BPM001] Skipped ControlRigBlueprint '%s'"), *Issue->FilePath);
						++BPSkipped;
						continue;
					}
					FBlueprintEditorUtils::RemoveMemberVariable(BP, FName(*VarName));
					FKismetEditorUtilities::CompileBlueprint(BP);
					(void)BP->MarkPackageDirty();
					UE_LOG(LogShintTools, Log,
						TEXT("ApplyFix: [BPM001] Removed variable '%s' from '%s'"), *VarName, *Issue->FilePath);
					++BPApplied;
					continue;
				}
			}
			UE_LOG(LogShintTools, Warning,
				TEXT("ApplyFix: [BPM001] Could not fix '%s' — VarName='%s'"), *Issue->FilePath, *VarName);
			++BPSkipped;
		}
		else if (Issue->RuleId == TEXT("BPM002"))
		{
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *MakeBPPath(Issue->FilePath));
			if (BP)
			{
				if (IsControlRigBP(BP))
				{
					UE_LOG(LogShintTools, Log,
						TEXT("ApplyFix: [BPM002] Skipped ControlRigBlueprint '%s'"), *Issue->FilePath);
					++BPSkipped;
					continue;
				}
				TArray<UEdGraph*> AllGraphs;
				AllGraphs.Append(BP->UbergraphPages);
				AllGraphs.Append(BP->FunctionGraphs);

				int32 RemovedNodes = 0;
				for (UEdGraph* Graph : AllGraphs)
				{
					if (!Graph) continue;
					for (int32 NodeIdx = Graph->Nodes.Num() - 1; NodeIdx >= 0; --NodeIdx)
					{
						UEdGraphNode* Node = Graph->Nodes[NodeIdx];
						if (!Node) continue;

						if (Node->IsA<UK2Node_FunctionEntry>()) continue;
						if (Node->GetClass()->GetName().Contains(TEXT("Event"))) continue;
						if (Node->GetClass()->GetName().Contains(TEXT("Tunnel"))) continue;

						bool bAllDisconnected = true;
						for (UEdGraphPin* Pin : Node->Pins)
						{
							if (Pin && Pin->LinkedTo.Num() > 0)
							{
								bAllDisconnected = false;
								break;
							}
						}

						if (bAllDisconnected)
						{
							FBlueprintEditorUtils::RemoveNode(BP, Node, /*bDontRecompile=*/true);
							++RemovedNodes;
						}
					}
				}

				if (RemovedNodes > 0)
				{
					FKismetEditorUtilities::CompileBlueprint(BP);
					(void)BP->MarkPackageDirty();
					UE_LOG(LogShintTools, Log,
						TEXT("ApplyFix: [BPM002] Removed %d disconnected node(s) from '%s'"),
						RemovedNodes, *Issue->FilePath);
					++BPApplied;
					continue;
				}
			}
			UE_LOG(LogShintTools, Warning,
				TEXT("ApplyFix: [BPM002] Could not fix '%s'"), *Issue->FilePath);
			++BPSkipped;
		}
		else if (Issue->RuleId == TEXT("BPB007"))
		{
			FString VarName;
			const FString& Msg = Issue->Message;
			int32 Q1 = INDEX_NONE, Q2 = INDEX_NONE;
			Msg.FindChar(TCHAR('\''), Q1);
			if (Q1 != INDEX_NONE)
				Q2 = Msg.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q1 + 1);

			if (Q1 != INDEX_NONE && Q2 != INDEX_NONE && Q2 > Q1)
				VarName = Msg.Mid(Q1 + 1, Q2 - Q1 - 1);

			if (!VarName.IsEmpty())
			{
				UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *MakeBPPath(Issue->FilePath));
				if (BP)
				{
					bool bFound = false;
					for (FBPVariableDescription& Var : BP->NewVariables)
					{
						if (Var.VarName == FName(*VarName))
						{
							Var.Category = NSLOCTEXT("ShintTools", "DefaultCategory", "Default");
							bFound = true;
							break;
						}
					}
					if (bFound)
					{
						FBlueprintEditorUtils::RefreshAllNodes(BP);
						FKismetEditorUtilities::CompileBlueprint(BP);
						(void)BP->MarkPackageDirty();
						UE_LOG(LogShintTools, Log,
							TEXT("ApplyFix: [BPB007] Set category 'Default' on '%s' in '%s'"),
							*VarName, *Issue->FilePath);
						++BPApplied;
						continue;
					}
				}
			}
			UE_LOG(LogShintTools, Warning,
				TEXT("ApplyFix: [BPB007] Could not fix '%s' — VarName='%s'"), *Issue->FilePath, *VarName);
			++BPSkipped;
		}
		else if (Issue->RuleId == TEXT("BPB001"))
		{
			// W-003: BPB001 (BP naming) is handled by the Asset Naming pipeline
			// (see SShintToolsPanel::OnBlueprintValidateComplete).
			++BPSkipped;
		}
		else
		{
			UE_LOG(LogShintTools, Verbose,
				TEXT("ApplyFix: BP rule '%s' has no plugin-side handler — skipping"), *Issue->RuleId);
			++BPSkipped;
		}
	}

	FShintFixResult Result;
	Result.bSuccess = true;

	if (!LocalIssues.IsEmpty())
	{
		TMap<FString, TArray<const FShintCodeIssue*>> ByFile;
		for (const FShintCodeIssue& Issue : LocalIssues)
			ByFile.FindOrAdd(Issue.FilePath).Add(&Issue);

		for (auto& Pair : ByFile)
		{
			const FString& AbsPath = Pair.Key;
			const TArray<const FShintCodeIssue*>& Issues = Pair.Value;

			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *AbsPath))
			{
				Result.TotalFixesSkipped += Issues.Num();
				continue;
			}

			TArray<FString> Lines;
			Content.ParseIntoArray(Lines, TEXT("\n"), false);

			TArray<const FShintCodeIssue*> Sorted = Issues;
			// References (NOT pointers) in the comparator are correct here:
			// UE5's TArray<T*>::Sort wraps the user predicate in a dereferencing
			// adapter, so the lambda receives the pointed-to elements.
			Sorted.Sort([](const FShintCodeIssue& A, const FShintCodeIssue& B) { return A.Line > B.Line; });

			int32 Applied = 0, Skipped = 0;
			for (const FShintCodeIssue* Issue : Sorted)
			{
				const int32 Idx = Issue->Line - 1;
				if (Idx < 0 || Idx >= Lines.Num()) { ++Skipped; continue; }

				FString Leading;
				for (TCHAR Ch : Lines[Idx]) { if (Ch == ' ' || Ch == '\t') Leading.AppendChar(Ch); else break; }

				Lines[Idx] = Leading + Issue->FixSuggestion.TrimStartAndEnd();
				++Applied;
			}

			if (Applied > 0)
			{
				const FString NewContent = FString::Join(Lines, TEXT("\n"));
				if (FFileHelper::SaveStringToFile(NewContent, *AbsPath,
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
				{
					FShintFixedFile FF;
					FF.FilePath = AbsPath; FF.CorrectedContent = NewContent;
					FF.FixesApplied = Applied; FF.FixesSkipped = Skipped;
					Result.FixedFiles.Add(MoveTemp(FF));
				}
			}
			Result.TotalFixesApplied += Applied;
			Result.TotalFixesSkipped += Skipped;
		}
	}

	Result.TotalFixesApplied += BPApplied;
	Result.TotalFixesSkipped += BPSkipped + SkippedNoFix;

	if (!TreeSitterIssues.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> IssuesArr;
		for (const FShintCodeIssue& Issue : TreeSitterIssues)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("rule_id"),   Issue.RuleId);
			O->SetStringField(TEXT("file_path"), Issue.FilePath);
			O->SetNumberField(TEXT("line"),      Issue.Line);
			O->SetStringField(TEXT("content"),   Issue.FileContent);
			IssuesArr.Add(MakeShared<FJsonValueObject>(O));
		}
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetArrayField(TEXT("issues"), IssuesArr);

		for (const FShintCodeIssue& DbgIssue : TreeSitterIssues)
		{
			UE_LOG(LogShintTools, Log,
				TEXT("ApplyFix [TS]: rule=%s file=%s line=%d contentLen=%d"),
				*DbgIssue.RuleId, *DbgIssue.FilePath, DbgIssue.Line, DbgIssue.FileContent.Len());
		}
		UE_LOG(LogShintTools, Log, TEXT("ApplyFix: Sending %d issue(s) to tree-sitter /validate/fix"),
			TreeSitterIssues.Num());

		SendRequest(Config.GetBaseUrl() + TEXT("/validate/fix"),
			EShintHttpMethod::POST, SerializeJson(Body),
			FOnShintRequestComplete::CreateSP(this, &FShintCoreClient::HandleTreeSitterFixResponse,
				Result, TreeSitterIssues, OnComplete));
		return;
	}

	UE_LOG(LogShintTools, Log,
		TEXT("[BENCH] ApplyCodeFixes (local): %.3f s, %d applied, %d skipped"),
		FPlatformTime::Seconds() - BenchFixStart, Result.TotalFixesApplied, Result.TotalFixesSkipped);

	if (Result.FixedFiles.Num() > 0)
	{
		const FString BuildBat = FPaths::ConvertRelativePathToFull(
			FPaths::EngineDir() / TEXT("Build/BatchFiles/Build.bat"));
		const FString UProjectPath = FPaths::ConvertRelativePathToFull(
			FPaths::GetProjectFilePath());
		const FString TargetName = FString(FApp::GetProjectName()) + TEXT("Editor");
		const FString BuildArgs  = FString::Printf(
			TEXT("%s Win64 Development -project=\"%s\" -NoHotReloadFromIDE"),
			*TargetName, *UProjectPath);

		UE_LOG(LogShintTools, Log, TEXT("ApplyFix: Launching incremental build check: %s %s"),
			*BuildBat, *BuildArgs);

		Async(EAsyncExecution::Thread, [BuildBat, BuildArgs, Result, OnComplete]() mutable
		{
			FString StdOut, StdErr;
			int32   ExitCode = 0;
			FPlatformProcess::ExecProcess(
				*BuildBat, *BuildArgs, &ExitCode, &StdOut, &StdErr,
				/*WorkingDir=*/nullptr, /*bShouldEndWithParentProcess=*/false);

			const FString FullOutput = StdOut + StdErr;

			if (ExitCode != 0)
			{
				Result.bHasCompileErrors = true;

				// MSVC: path(line): error/warning CODE: message
				// Clang: path/file.cpp:line:col: error: ...
				static const FString ErrorKeyword   = TEXT("): error ");
				static const FString WarningKeyword = TEXT("): warning ");

				TArray<FString> OutputLines;
				FullOutput.ParseIntoArrayLines(OutputLines);

				for (const FString& OutLine : OutputLines)
				{
					int32 ParenClose = INDEX_NONE;
					int32 ParenOpen  = INDEX_NONE;
					if (!OutLine.FindLastChar(TEXT(')'), ParenClose)) continue;
					for (int32 c = ParenClose - 1; c >= 0; --c)
					{
						if (OutLine[c] == TEXT('('))
						{
							ParenOpen = c;
							break;
						}
					}
					if (ParenOpen == INDEX_NONE) continue;

					const FString MaybeFile = OutLine.Left(ParenOpen);
					const FString MaybeLine = OutLine.Mid(ParenOpen + 1, ParenClose - ParenOpen - 1);
					if (!MaybeLine.IsNumeric()) continue;

					const FString Ext = FPaths::GetExtension(MaybeFile).ToLower();
					if (Ext != TEXT("cpp") && Ext != TEXT("h") && Ext != TEXT("cc") && Ext != TEXT("hpp"))
						continue;

					FString Severity;
					int32   SevStart = INDEX_NONE;
					SevStart = OutLine.Find(TEXT("): error "), ESearchCase::IgnoreCase, ESearchDir::FromStart, ParenClose);
					if (SevStart != INDEX_NONE)
					{
						Severity = TEXT("error");
					}
					else
					{
						SevStart = OutLine.Find(TEXT("): warning "), ESearchCase::IgnoreCase, ESearchDir::FromStart, ParenClose);
						if (SevStart != INDEX_NONE)
						{
							Severity = TEXT("warning");
						}
					}

					const int32 AfterParen = ParenClose + 1;
					FString Rest = OutLine.Mid(AfterParen).TrimStart();
					FString Code, Message;
					int32 ColonIdx = INDEX_NONE;
					if (Rest.FindChar(TEXT(':'), ColonIdx))
					{
						const int32 SpaceIdx = Rest.Find(TEXT(" "), ESearchCase::IgnoreCase,
							ESearchDir::FromStart, 0);
						if (SpaceIdx != INDEX_NONE && SpaceIdx < ColonIdx)
						{
							Code    = Rest.Mid(SpaceIdx + 1, ColonIdx - SpaceIdx - 1).TrimStartAndEnd();
							Message = Rest.Mid(ColonIdx + 1).TrimStart();
						}
						else
						{
							Message = Rest.Mid(ColonIdx + 1).TrimStart();
						}
					}
					else
					{
						Message = Rest;
					}

					if (Message.IsEmpty()) continue;

					FShintCompileError CE;
					CE.FilePath  = MaybeFile;
					CE.FileName  = FPaths::GetCleanFilename(MaybeFile);
					CE.Line      = FCString::Atoi(*MaybeLine);
					CE.Code      = Code;
					CE.Message   = Message.Left(200);
					CE.Severity  = Severity;
					Result.CompileErrors.Add(MoveTemp(CE));
				}

				UE_LOG(LogShintTools, Warning,
					TEXT("ApplyFix: Build failed — %d compile error(s) detected"),
					Result.CompileErrors.Num());
			}
			else
			{
				UE_LOG(LogShintTools, Log, TEXT("ApplyFix: Build succeeded — no compile errors"));
			}

			AsyncTask(ENamedThreads::GameThread, [Result, OnComplete]() mutable
			{
				OnComplete.ExecuteIfBound(Result);
			});
		});

		return;
	}

	UE_LOG(LogShintTools, Log,
		TEXT("[BENCH] ApplyCodeFixes (no build): %.3f s, %d applied, %d skipped"),
		FPlatformTime::Seconds() - BenchFixStart, Result.TotalFixesApplied, Result.TotalFixesSkipped);
	OnComplete.ExecuteIfBound(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tree-sitter — single-issue fix preview (no disk write)
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::FetchSingleFixPreview(
	const FShintCodeIssue& Issue, FOnShintFixComplete OnComplete)
{
	if (Issue.FileContent.IsEmpty())
	{
		FShintFixResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("FetchSingleFixPreview: no FileContent on issue");
		OnComplete.ExecuteIfBound(Err);
		return;
	}

	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("rule_id"),   Issue.RuleId);
	O->SetStringField(TEXT("file_path"), Issue.FilePath);
	O->SetNumberField(TEXT("line"),      Issue.Line);
	O->SetStringField(TEXT("content"),   Issue.FileContent);

	TArray<TSharedPtr<FJsonValue>> IssuesArr;
	IssuesArr.Add(MakeShared<FJsonValueObject>(O));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("issues"), IssuesArr);

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/fix"),
		EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable
		{
			OnComplete.ExecuteIfBound(FShintCoreClient::ParseTreeSitterFixResponse(Raw));
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tree-sitter fix response — parse + write to disk + incremental build
// ─────────────────────────────────────────────────────────────────────────────

FShintFixResult FShintCoreClient::ParseTreeSitterFixResponse(const FShintRequestResult& Raw)
{
	FShintFixResult Result;
	Result.bSuccess = Raw.bSuccess;

	if (!Raw.bSuccess)
	{
		Result.ErrorMessage = Raw.ErrorMessage;
		return Result;
	}

	UE_LOG(LogShintTools, Log, TEXT("TreeSitterFix response (first 1000): %s"), *Raw.ResponseBody.Left(1000));

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.bSuccess     = false;
		Result.ErrorMessage = TEXT("ParseTreeSitterFixResponse: invalid JSON");
		return Result;
	}

	const TSharedPtr<FJsonObject>* SummaryObj = nullptr;
	if (Root->TryGetObjectField(TEXT("summary"), SummaryObj) && SummaryObj)
	{
		(*SummaryObj)->TryGetNumberField(TEXT("successful"), Result.TotalFixesApplied);
		(*SummaryObj)->TryGetNumberField(TEXT("failed"),     Result.TotalFixesSkipped);
	}

	const TArray<TSharedPtr<FJsonValue>>* FixesArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("fixes"), FixesArr) || !FixesArr) return Result;

	for (const TSharedPtr<FJsonValue>& FixVal : *FixesArr)
	{
		const TSharedPtr<FJsonObject>* FixObjPtr = nullptr;
		if (!FixVal->TryGetObject(FixObjPtr) || !FixObjPtr) continue;
		const TSharedPtr<FJsonObject>& Fix = *FixObjPtr;

		bool bOk = false;
		Fix->TryGetBoolField(TEXT("success"), bOk);
		if (!bOk) continue;

		FShintFixedFile FF;
		FF.FixesApplied = 1;
		FF.FixesSkipped = 0;
		Fix->TryGetStringField(TEXT("file_path"),  FF.FilePath);
		Fix->TryGetStringField(TEXT("fixed_code"), FF.CorrectedContent);
		Fix->TryGetStringField(TEXT("additions"),  FF.Additions);

		const TArray<TSharedPtr<FJsonValue>>* ChangesArr = nullptr;
		if (Fix->TryGetArrayField(TEXT("changes"), ChangesArr) && ChangesArr)
		{
			for (const TSharedPtr<FJsonValue>& C : *ChangesArr)
				FF.Changes.Add(C->AsString());
		}

		if (!FF.FilePath.IsEmpty() && !FF.CorrectedContent.IsEmpty())
			Result.FixedFiles.Add(MoveTemp(FF));
	}

	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Safety Check
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Local helper — not a class member, so the anonymous namespace keeps
	// it private to this TU. Mirrors what lived directly in the .cpp before
	// the split.
	FShintSafetyCheckResult ParseSafetyCheckResponse(const FShintRequestResult& Raw)
	{
		FShintSafetyCheckResult Res; // bSafe = true by default — never block on error
		if (!Raw.bSuccess || Raw.ResponseBody.IsEmpty())
			return Res;

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			return Res;

		bool bSafe = true;
		if (Root->TryGetBoolField(TEXT("safe"), bSafe))
			Res.bSafe = bSafe;

		const TArray<TSharedPtr<FJsonValue>>* Warns = nullptr;
		if (Root->TryGetArrayField(TEXT("warnings"), Warns) && Warns)
			for (const TSharedPtr<FJsonValue>& W : *Warns)
				Res.Warnings.Add(W->AsString());

		FString Preview;
		if (Root->TryGetStringField(TEXT("preview"), Preview))
			Res.Preview = MoveTemp(Preview);

		return Res;
	}
}

void FShintCoreClient::CheckFixSafety(
	const TArray<FShintCodeIssue>& Issues, FOnShintSafetyCheckComplete OnComplete)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> IssueArr;
	for (const FShintCodeIssue& Issue : Issues)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("rule_id"),        Issue.RuleId);
		Obj->SetStringField(TEXT("file_path"),      Issue.FilePath);
		Obj->SetStringField(TEXT("snippet"),        Issue.Snippet);
		Obj->SetStringField(TEXT("fix_suggestion"), Issue.FixSuggestion);
		IssueArr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Root->SetArrayField(TEXT("issues"), IssueArr);

	const FString Url = Config.GetBaseUrl() / TEXT("validate/check-fix-safety");
	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Root),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete = MoveTemp(OnComplete)](const FShintRequestResult& Raw) mutable
			{
				OnComplete.ExecuteIfBound(ParseSafetyCheckResponse(Raw));
			}));
}

void FShintCoreClient::HandleTreeSitterFixResponse(
	const FShintRequestResult& Raw,
	FShintFixResult             LocalResult,
	TArray<FShintCodeIssue>     TreeSitterIssues,
	FOnShintFixComplete         OnComplete)
{
	FShintFixResult TSResult = ParseTreeSitterFixResponse(Raw);

	if (!TSResult.bSuccess)
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("HandleTreeSitterFixResponse: server error — %s"), *TSResult.ErrorMessage);
	}
	else
	{
		for (const FShintFixedFile& FF : TSResult.FixedFiles)
		{
			if (FF.CorrectedContent.IsEmpty()) continue;
			if (!FFileHelper::SaveStringToFile(FF.CorrectedContent, *FF.FilePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogShintTools, Warning,
					TEXT("HandleTreeSitterFixResponse: failed to write '%s'"), *FF.FilePath);
			}
			else
			{
				UE_LOG(LogShintTools, Log,
					TEXT("HandleTreeSitterFixResponse: wrote fixed file '%s'"), *FF.FilePath);
			}
		}
	}

	LocalResult.FixedFiles.Append(TSResult.FixedFiles);
	LocalResult.TotalFixesApplied += TSResult.TotalFixesApplied;
	LocalResult.TotalFixesSkipped += TSResult.TotalFixesSkipped;
	if (!TSResult.bSuccess && LocalResult.bSuccess)
	{
		LocalResult.ErrorMessage = TSResult.ErrorMessage;
	}

	UE_LOG(LogShintTools, Log,
		TEXT("HandleTreeSitterFixResponse: merged — %d applied, %d skipped, %d fixed files"),
		LocalResult.TotalFixesApplied, LocalResult.TotalFixesSkipped, LocalResult.FixedFiles.Num());

	if (LocalResult.FixedFiles.Num() > 0)
	{
		const FString BuildBat = FPaths::ConvertRelativePathToFull(
			FPaths::EngineDir() / TEXT("Build/BatchFiles/Build.bat"));
		const FString UProjectPath = FPaths::ConvertRelativePathToFull(
			FPaths::GetProjectFilePath());
		const FString TargetName = FString(FApp::GetProjectName()) + TEXT("Editor");
		const FString BuildArgs  = FString::Printf(
			TEXT("%s Win64 Development -project=\"%s\" -NoHotReloadFromIDE"),
			*TargetName, *UProjectPath);

		UE_LOG(LogShintTools, Log, TEXT("HandleTreeSitterFixResponse: launching incremental build: %s %s"),
			*BuildBat, *BuildArgs);

		Async(EAsyncExecution::Thread, [BuildBat, BuildArgs, LocalResult, OnComplete]() mutable
		{
			FString StdOut, StdErr;
			int32   ExitCode = 0;
			FPlatformProcess::ExecProcess(
				*BuildBat, *BuildArgs, &ExitCode, &StdOut, &StdErr,
				/*WorkingDir=*/nullptr, /*bShouldEndWithParentProcess=*/false);

			const FString FullOutput = StdOut + StdErr;

			if (ExitCode != 0)
			{
				LocalResult.bHasCompileErrors = true;

				TArray<FString> OutputLines;
				FullOutput.ParseIntoArrayLines(OutputLines);

				for (const FString& OutLine : OutputLines)
				{
					int32 ParenClose = INDEX_NONE;
					int32 ParenOpen  = INDEX_NONE;
					if (!OutLine.FindLastChar(TEXT(')'), ParenClose)) continue;
					for (int32 c = ParenClose - 1; c >= 0; --c)
					{
						if (OutLine[c] == TEXT('(')) { ParenOpen = c; break; }
					}
					if (ParenOpen == INDEX_NONE) continue;

					const FString MaybeFile = OutLine.Left(ParenOpen);
					const FString MaybeLine = OutLine.Mid(ParenOpen + 1, ParenClose - ParenOpen - 1);
					if (!MaybeLine.IsNumeric()) continue;

					const FString Ext = FPaths::GetExtension(MaybeFile).ToLower();
					if (Ext != TEXT("cpp") && Ext != TEXT("h") && Ext != TEXT("cc") && Ext != TEXT("hpp"))
						continue;

					FString Severity;
					int32 SevStart = OutLine.Find(TEXT("): error "),   ESearchCase::IgnoreCase, ESearchDir::FromStart, ParenClose);
					if (SevStart != INDEX_NONE) Severity = TEXT("error");
					else
					{
						SevStart = OutLine.Find(TEXT("): warning "), ESearchCase::IgnoreCase, ESearchDir::FromStart, ParenClose);
						if (SevStart != INDEX_NONE) Severity = TEXT("warning");
					}

					const int32 AfterParen = ParenClose + 1;
					FString Rest = OutLine.Mid(AfterParen).TrimStart();
					FString Code, Message;
					int32 ColonIdx = INDEX_NONE;
					if (Rest.FindChar(TEXT(':'), ColonIdx))
					{
						const int32 SpaceIdx = Rest.Find(TEXT(" "), ESearchCase::IgnoreCase, ESearchDir::FromStart, 0);
						if (SpaceIdx != INDEX_NONE && SpaceIdx < ColonIdx)
						{
							Code    = Rest.Mid(SpaceIdx + 1, ColonIdx - SpaceIdx - 1).TrimStartAndEnd();
							Message = Rest.Mid(ColonIdx + 1).TrimStart();
						}
						else
						{
							Message = Rest.Mid(ColonIdx + 1).TrimStart();
						}
					}
					else { Message = Rest; }

					if (Message.IsEmpty()) continue;

					FShintCompileError CE;
					CE.FilePath = MaybeFile;
					CE.FileName = FPaths::GetCleanFilename(MaybeFile);
					CE.Line     = FCString::Atoi(*MaybeLine);
					CE.Code     = Code;
					CE.Message  = Message.Left(200);
					CE.Severity = Severity;
					LocalResult.CompileErrors.Add(MoveTemp(CE));
				}

				UE_LOG(LogShintTools, Warning,
					TEXT("HandleTreeSitterFixResponse: build failed — %d error(s)"),
					LocalResult.CompileErrors.Num());
			}
			else
			{
				UE_LOG(LogShintTools, Log, TEXT("HandleTreeSitterFixResponse: build succeeded"));
			}

			AsyncTask(ENamedThreads::GameThread, [LocalResult, OnComplete]() mutable
			{
				OnComplete.ExecuteIfBound(LocalResult);
			});
		});

		return;
	}

	OnComplete.ExecuteIfBound(LocalResult);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse helpers — Validator response shape
// ─────────────────────────────────────────────────────────────────────────────

FShintValidateResult FShintCoreClient::ParseValidateResponse(const FShintRequestResult& Raw)
{
	FShintValidateResult R;
	R.StatusCode = Raw.StatusCode;
	if (!Raw.bSuccess) { R.bSuccess = false; R.ErrorMessage = Raw.ErrorMessage; return R; }

	TSharedPtr<FJsonObject> J;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, J) || !J.IsValid())
	{ R.bSuccess = false; R.ErrorMessage = TEXT("Failed to parse validate response."); return R; }

	R.bSuccess = true;

	UE_LOG(LogShintTools, Log, TEXT("ParseValidate: Response length=%d, first 500 chars: %s"),
		Raw.ResponseBody.Len(), *Raw.ResponseBody.Left(500));

	const TSharedPtr<FJsonObject>* Sum = nullptr;
	if (J->TryGetObjectField(TEXT("summary"), Sum) && Sum)
	{
		(*Sum)->TryGetNumberField(TEXT("total"),         R.TotalIssues);
		(*Sum)->TryGetNumberField(TEXT("errors"),        R.TotalErrors);
		(*Sum)->TryGetNumberField(TEXT("warnings"),      R.TotalWarnings);
		(*Sum)->TryGetNumberField(TEXT("files_scanned"), R.FilesScanned);

		(*Sum)->TryGetBoolField  (TEXT("limit_applied"),   R.bLimitApplied);
		(*Sum)->TryGetStringField(TEXT("limit_kind"),      R.LimitKind);
		(*Sum)->TryGetNumberField(TEXT("limit_value"),     R.LimitValue);
		(*Sum)->TryGetNumberField(TEXT("total_available"), R.TotalAvailable);

		UE_LOG(LogShintTools, Log, TEXT("ParseValidate: summary total=%d errors=%d warnings=%d files=%d (limit_applied=%d %s %d/%d)"),
			R.TotalIssues, R.TotalErrors, R.TotalWarnings, R.FilesScanned,
			R.bLimitApplied ? 1 : 0, *R.LimitKind, R.LimitValue, R.TotalAvailable);
	}

	{
		double Q = -1.0;
		if (J->TryGetNumberField(TEXT("quality_score"), Q))
		{
			R.QualityScoreOverall = static_cast<float>(Q);
			UE_LOG(LogShintTools, Log, TEXT("ParseValidate: quality_score=%.1f"), R.QualityScoreOverall);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* IssArr = nullptr;
	if (J->TryGetArrayField(TEXT("issues"), IssArr) && IssArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *IssArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;
			FShintCodeIssue Issue;
			(*O)->TryGetStringField(TEXT("rule_id"),        Issue.RuleId);
			(*O)->TryGetStringField(TEXT("rule_name"),        Issue.RuleName);
			(*O)->TryGetStringField(TEXT("rule_explanation"), Issue.RuleExplanation);
			(*O)->TryGetStringField(TEXT("severity"),       Issue.Severity);
			(*O)->TryGetStringField(TEXT("message"),        Issue.Message);
			if (!(*O)->TryGetStringField(TEXT("file_path"), Issue.FilePath))
				(*O)->TryGetStringField(TEXT("asset_path"), Issue.FilePath);
			(*O)->TryGetNumberField(TEXT("line"),           Issue.Line);
			(*O)->TryGetStringField(TEXT("snippet"),             Issue.Snippet);
			(*O)->TryGetStringField(TEXT("fix_suggestion"),      Issue.FixSuggestion);
			(*O)->TryGetStringField(TEXT("class"),               Issue.Class);
			(*O)->TryGetStringField(TEXT("category"),            Issue.Category);
			(*O)->TryGetStringField(TEXT("graph"),               Issue.Graph);
			(*O)->TryGetStringField(TEXT("content"),             Issue.FileContent);
			(*O)->TryGetStringField(TEXT("context_before"),      Issue.ContextBefore);
			(*O)->TryGetStringField(TEXT("context_after"),       Issue.ContextAfter);
			int32 CtxStart = 0;
			(*O)->TryGetNumberField(TEXT("context_line_start"),  CtxStart);
			Issue.ContextLineStart = CtxStart;

			// If the server did not echo back the file content (validate endpoint does not
			// inject it), read it from disk so FetchFixPreview can call /validate/fix.
			if (Issue.FileContent.IsEmpty()
				&& !Issue.FilePath.IsEmpty()
				&& !Issue.FilePath.StartsWith(TEXT("/Game/"))
				&& !Issue.FilePath.StartsWith(TEXT("/Engine/")))
			{
				FFileHelper::LoadFileToString(Issue.FileContent, *Issue.FilePath);
			}

			bool bServerFixable = false;
			(*O)->TryGetBoolField(TEXT("is_auto_fixable"), bServerFixable);
			Issue.bIsAutoFixable = bServerFixable || !Issue.FixSuggestion.IsEmpty();
			Issue.bChecked = Issue.bIsAutoFixable;
			R.Issues.Add(MoveTemp(Issue));
		}
		UE_LOG(LogShintTools, Log, TEXT("ParseValidate: Parsed %d issues from 'issues' array (array had %d entries)"),
			R.Issues.Num(), IssArr->Num());
	}
	else
	{
		UE_LOG(LogShintTools, Warning, TEXT("ParseValidate: No 'issues' array found in response"));
	}
	return R;
}

FShintFixResult FShintCoreClient::ParseFixResponse(const FShintRequestResult& Raw)
{
	FShintFixResult R;
	if (!Raw.bSuccess) { R.bSuccess = false; R.ErrorMessage = Raw.ErrorMessage; return R; }

	TSharedPtr<FJsonObject> J;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, J) || !J.IsValid())
	{ R.bSuccess = false; R.ErrorMessage = TEXT("Failed to parse fix response."); return R; }

	R.bSuccess = true;
	J->TryGetNumberField(TEXT("total_fixes_applied"), R.TotalFixesApplied);
	J->TryGetNumberField(TEXT("total_fixes_skipped"), R.TotalFixesSkipped);

	const TArray<TSharedPtr<FJsonValue>>* FilesArr = nullptr;
	if (!J->TryGetArrayField(TEXT("fixed_files"), FilesArr) || !FilesArr)
		J->TryGetArrayField(TEXT("files"), FilesArr);

	if (FilesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *FilesArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;
			FShintFixedFile FF;
			if (!(*O)->TryGetStringField(TEXT("file_path"), FF.FilePath))
				(*O)->TryGetStringField(TEXT("path"), FF.FilePath);
			if (!(*O)->TryGetStringField(TEXT("corrected_content"), FF.CorrectedContent))
				(*O)->TryGetStringField(TEXT("content"), FF.CorrectedContent);
			(*O)->TryGetNumberField(TEXT("fixes_applied"), FF.FixesApplied);
			(*O)->TryGetNumberField(TEXT("fixes_skipped"), FF.FixesSkipped);

			if (FF.FixesApplied == 0 && !FF.CorrectedContent.IsEmpty())
				FF.FixesApplied = 1;

			R.FixedFiles.Add(MoveTemp(FF));
		}
	}
	else
	{
		UE_LOG(LogShintTools, Warning, TEXT("ParseFix: No 'fixed_files' or 'files' array in response"));
	}
	return R;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::CollectSourceFiles(const FString& Dir, TArray<FString>& Out)
{
	TArray<FString> Cpp, H;
	IFileManager::Get().FindFilesRecursive(Cpp, *Dir, TEXT("*.cpp"), true, false);
	IFileManager::Get().FindFilesRecursive(H,   *Dir, TEXT("*.h"),   true, false);
	Out.Append(Cpp); Out.Append(H);
}
