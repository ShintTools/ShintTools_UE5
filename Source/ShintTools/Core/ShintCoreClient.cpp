// Copyright ShintTools. All Rights Reserved.
#include "ShintCoreClient.h"
#include "ShintTools/ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

// ─── JSON micro-helpers ───────────────────────────────────────────────────────

static TSharedRef<FJsonObject> JO()  { return MakeShared<FJsonObject>(); }
static TSharedPtr<FJsonValue>  JS(const FString& S) { return MakeShared<FJsonValueString>(S); }
static TSharedPtr<FJsonValue>  JV(TSharedRef<FJsonObject> O) { return MakeShared<FJsonValueObject>(O); }

FString FShintClient::ToJson(TSharedRef<FJsonObject> O)
{
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(O, W); return S;
}

// ─── Construction / Config ────────────────────────────────────────────────────

FShintClient::FShintClient() { LoadConfig(); }

bool FShintClient::LoadConfig()
{
	C = FShintCfg();
	const FString P = FPaths::Combine(FPaths::ProjectDir(), TEXT("shinttools.config.json"));
	if (!FPaths::FileExists(P)) return false;
	FString Raw; if (!FFileHelper::LoadFileToString(Raw, *P)) return false;
	TSharedPtr<FJsonObject> J; TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, J) || !J.IsValid()) return false;

	int32 Port=0; if (J->TryGetNumberField(TEXT("core_port"),Port)&&Port>0) C.Port=Port;
	FString S;
	if (J->TryGetStringField(TEXT("project_name"), S)) C.Name      = S;
	if (J->TryGetStringField(TEXT("project_id"),   S)) C.ProjectId = S;
	if (J->TryGetStringField(TEXT("api_key"),      S)) C.Key       = S;
	if (J->TryGetStringField(TEXT("dashboard_url"),S)) C.DashUrl   = S;
	UE_LOG(LogShintTools,Log,TEXT("ShintClient: Port=%d Key=%s"),C.Port,C.HasDash()?TEXT("ok"):TEXT("missing"));
	return true;
}

// ─── Connectivity ─────────────────────────────────────────────────────────────

void FShintClient::Health(FOnRaw Done) { Get(C.Base()+TEXT("/health"), Done); }

// ─── ScanProject ─────────────────────────────────────────────────────────────

void FShintClient::ScanProject(const FString& SrcDir, FOnValidate Done)
{
	TArray<FString> Abs; CollectCpp(SrcDir, Abs);
	UE_LOG(LogShintTools,Log,TEXT("ShintClient: ScanProject — %d files"),Abs.Num());

	TArray<TSharedPtr<FJsonValue>> FA;
	for (const FString& F : Abs)
	{
		FString Cnt; if (!FFileHelper::LoadFileToString(Cnt,*F)) continue;
		TSharedRef<FJsonObject> O=JO(); O->SetStringField(TEXT("file_path"),F); O->SetStringField(TEXT("content"),Cnt);
		FA.Add(JV(O));
	}
	TSharedRef<FJsonObject> B=JO(); B->SetArrayField(TEXT("files"),FA); B->SetStringField(TEXT("engine"),TEXT("unreal"));
	TArray<FString> Cap=Abs;
	Post(C.Base()+TEXT("/validate/project"), ToJson(B),
		FOnRaw::CreateLambda([Done,Cap](const FShintRaw& Raw) mutable {
			FValidateResult R = ParseValidate(Raw); R.ScannedPaths=Cap; Done.ExecuteIfBound(R);
		}));
}

// ─── ReadBP (editor API only — no K2Node specialisations) ────────────────────

TSharedPtr<FJsonObject> FShintClient::ReadBP(const FString& ObjPath) const
{
	UBlueprint* BP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(),nullptr,*ObjPath));
	if (!BP) return nullptr;

	FString BpType=TEXT("Actor");
	if (BP->ParentClass) {
		const FString P=BP->ParentClass->GetName();
		if      (P.Contains(TEXT("Character"))) BpType=TEXT("Character");
		else if (P.Contains(TEXT("Pawn")))       BpType=TEXT("Pawn");
		else if (P.Contains(TEXT("Widget")))     BpType=TEXT("Widget");
		else if (P.Contains(TEXT("Component")))  BpType=TEXT("Component");
	}

	int32 TotalNodes=0, TotalCasts=0, Disconn=0; bool bTick=false;

	auto ProcGraph=[&](UEdGraph* G, const FString& GType) -> TSharedPtr<FJsonValue>
	{
		if(!G) return nullptr;
		int32 NC=0,Conns=0,Casts=0; TMap<FString,int32> Types;
		for(UEdGraphNode* N : G->Nodes)
		{
			if(!N) continue; ++NC;
			for(UEdGraphPin* Pin : N->Pins) if(Pin&&Pin->Direction==EGPD_Output) Conns+=Pin->LinkedTo.Num();
			const FString Cls=N->GetClass()->GetName();
			Types.FindOrAdd(Cls)++;
			const FString Title=N->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if(Title.Contains(TEXT("Tick"))) bTick=true;
			if(Cls.Contains(TEXT("Cast"))||Cls.Contains(TEXT("DynamicCast"))) ++Casts;
			bool bAny=false; for(UEdGraphPin* Pin:N->Pins) if(Pin&&Pin->LinkedTo.Num()>0){bAny=true;break;}
			if(!bAny&&NC>1) ++Disconn;
		}
		TotalNodes+=NC; TotalCasts+=Casts;
		TArray<TPair<FString,int32>> Sorted(Types.Array());
		Sorted.Sort([](const TPair<FString,int32>&A,const TPair<FString,int32>&B){return A.Value>B.Value;});
		TArray<TSharedPtr<FJsonValue>> NA;
		for(int32 i=0;i<FMath::Min(5,Sorted.Num());++i){
			TSharedRef<FJsonObject> NO=JO(); NO->SetStringField(TEXT("type"),Sorted[i].Key); NO->SetNumberField(TEXT("count"),Sorted[i].Value); NA.Add(JV(NO));
		}
		TSharedRef<FJsonObject> GO=JO();
		GO->SetStringField(TEXT("name"),G->GetName()); GO->SetStringField(TEXT("type"),GType);
		GO->SetNumberField(TEXT("nodes_count"),NC); GO->SetNumberField(TEXT("connections"),Conns);
		GO->SetNumberField(TEXT("max_exec_depth"),NC/FMath::Max(1,4)); GO->SetNumberField(TEXT("cast_nodes"),Casts);
		GO->SetArrayField(TEXT("nodes"),NA);
		return JV(GO);
	};

	TArray<TSharedPtr<FJsonValue>> Graphs,Vars,Funcs;
	for(UEdGraph* G:BP->UbergraphPages) { auto V=ProcGraph(G,TEXT("event_graph")); if(V.IsValid()) Graphs.Add(V); }
	for(UEdGraph* G:BP->FunctionGraphs){ auto V=ProcGraph(G,TEXT("function_graph")); if(V.IsValid()) Graphs.Add(V); }

	for(FBPVariableDescription& Var:BP->NewVariables)
	{
		const FString VN=Var.VarName.ToString(); bool bUsed=false;
		for(UEdGraph* G:BP->UbergraphPages){ if(!G) continue;
			for(UEdGraphNode* N:G->Nodes){ if(!N) continue;
				for(UEdGraphPin* Pin:N->Pins){ if(Pin&&Pin->PinName.ToString().Contains(VN)){bUsed=true;break;} }
				if(bUsed) break; } if(bUsed) break; }
		TSharedRef<FJsonObject> VO=JO(); VO->SetStringField(TEXT("name"),VN); VO->SetStringField(TEXT("type"),Var.VarType.PinCategory.ToString()); VO->SetBoolField(TEXT("used"),bUsed);
		Vars.Add(JV(VO));
	}
	for(UEdGraph* G:BP->FunctionGraphs){ if(!G) continue;
		int32 Cx=1; for(UEdGraphNode* N:G->Nodes) if(N&&N->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(TEXT("Branch"))) ++Cx;
		TSharedRef<FJsonObject> FO=JO(); FO->SetStringField(TEXT("name"),G->GetName()); FO->SetNumberField(TEXT("complexity"),Cx); FO->SetNumberField(TEXT("nodes_count"),G->Nodes.Num());
		Funcs.Add(JV(FO));
	}

	TSharedRef<FJsonObject> Stats=JO();
	Stats->SetNumberField(TEXT("total_nodes"),TotalNodes); Stats->SetNumberField(TEXT("cast_nodes"),TotalCasts);
	Stats->SetBoolField(TEXT("tick_enabled"),bTick); Stats->SetBoolField(TEXT("has_construction_script"),BP->SimpleConstructionScript!=nullptr);
	Stats->SetNumberField(TEXT("disconnected_nodes"),Disconn);

	TSharedRef<FJsonObject> Bp=JO();
	Bp->SetStringField(TEXT("name"),BP->GetName()); Bp->SetStringField(TEXT("path"),ObjPath);
	Bp->SetStringField(TEXT("type"),TEXT("blueprint")); Bp->SetStringField(TEXT("blueprint_type"),BpType);
	Bp->SetStringField(TEXT("parent_class"),BP->ParentClass?BP->ParentClass->GetName():TEXT("AActor"));
	Bp->SetArrayField(TEXT("graphs"),Graphs); Bp->SetArrayField(TEXT("variables"),Vars);
	Bp->SetArrayField(TEXT("functions"),Funcs); Bp->SetObjectField(TEXT("stats"),Stats);
	return Bp;
}

// ─── AnalyseBP — client-side rules ───────────────────────────────────────────

void FShintClient::AnalyseBP(const TSharedPtr<FJsonObject>& Bp, TArray<FShintIssue>& Out)
{
	if(!Bp.IsValid()) return;
	FString Name,Path; Bp->TryGetStringField(TEXT("name"),Name); Bp->TryGetStringField(TEXT("path"),Path);

	auto Add=[&](const FString& Rule, const FString& Sev, const FString& Msg,
		const FString& Snip=TEXT(""), const FString& Fix=TEXT("")) {
		FShintIssue I; I.Rule=Rule; I.Sev=Sev; I.Msg=Msg; I.File=Path; I.Snippet=Snip; I.FixHint=Fix; I.bFixable=false; Out.Add(I);
	};

	const TSharedPtr<FJsonObject>* SP=nullptr;
	if(Bp->TryGetObjectField(TEXT("stats"),SP)&&SP)
	{
		int32 Casts=0,Disc=0,Total=0; bool Tick=false;
		(*SP)->TryGetNumberField(TEXT("cast_nodes"),Casts);
		(*SP)->TryGetNumberField(TEXT("disconnected_nodes"),Disc);
		(*SP)->TryGetNumberField(TEXT("total_nodes"),Total);
		(*SP)->TryGetBoolField(TEXT("tick_enabled"),Tick);

		if(Casts>5) Add(TEXT("BP-CAST"),TEXT("warning"),
			FString::Printf(TEXT("[%s] High cast count (%d). Use interfaces to reduce coupling."),*Name,Casts),
			FString::Printf(TEXT("cast_nodes: %d"),Casts),
			TEXT("Replace Cast nodes with BlueprintImplementableEvent interfaces."));
		if(Tick&&Total>30) Add(TEXT("BP-TICK"),TEXT("warning"),
			FString::Printf(TEXT("[%s] Tick enabled with %d nodes. Move infrequent work to timers."),*Name,Total),
			TEXT("tick_enabled: true"),TEXT("Use GetWorldTimerManager().SetTimer() for periodic logic."));
		if(Disc>3) Add(TEXT("BP-DISC"),TEXT("warning"),
			FString::Printf(TEXT("[%s] %d disconnected node(s). Clean up unused nodes."),*Name,Disc),
			FString::Printf(TEXT("disconnected_nodes: %d"),Disc));
	}

	const TArray<TSharedPtr<FJsonValue>>* FA=nullptr;
	if(Bp->TryGetArrayField(TEXT("functions"),FA)&&FA)
	{
		for(const TSharedPtr<FJsonValue>& V:*FA){
			const TSharedPtr<FJsonObject>* O=nullptr; if(!V->TryGetObject(O)||!O) continue;
			FString FN; int32 Cx=0,NC=0;
			(*O)->TryGetStringField(TEXT("name"),FN); (*O)->TryGetNumberField(TEXT("complexity"),Cx); (*O)->TryGetNumberField(TEXT("nodes_count"),NC);
			if(Cx>10||NC>50) Add(TEXT("BP-CMPLX"),TEXT("warning"),
				FString::Printf(TEXT("[%s] Function '%s' too complex (cx=%d, nodes=%d). Split it."),*Name,*FN,Cx,NC),
				FString::Printf(TEXT("%s: cx=%d, nodes=%d"),*FN,Cx,NC));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* VA=nullptr;
	if(Bp->TryGetArrayField(TEXT("variables"),VA)&&VA)
	{
		for(const TSharedPtr<FJsonValue>& V:*VA){
			const TSharedPtr<FJsonObject>* O=nullptr; if(!V->TryGetObject(O)||!O) continue;
			FString VN; bool Used=true;
			(*O)->TryGetStringField(TEXT("name"),VN); (*O)->TryGetBoolField(TEXT("used"),Used);
			if(!Used) Add(TEXT("BP-UVAR"),TEXT("warning"),
				FString::Printf(TEXT("[%s] Variable '%s' declared but never used."),*Name,*VN),
				FString::Printf(TEXT("variable: %s"),*VN));
		}
	}
}

// ─── ScanBlueprints ───────────────────────────────────────────────────────────

void FShintClient::ScanBlueprints(const FString& ContentDir, FOnValidate Done)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter F; F.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"),TEXT("Blueprint")));
	F.PackagePaths.Add(FName(TEXT("/Game"))); F.bRecursivePaths=true; F.bRecursiveClasses=true;
	TArray<FAssetData> Assets; ARM.Get().GetAssets(F,Assets);

	UE_LOG(LogShintTools,Log,TEXT("ShintClient: ScanBlueprints — %d BPs"),Assets.Num());

	BPs.Empty();
	TArray<FShintIssue> Issues;
	TArray<TSharedPtr<FJsonValue>> FilesArr;

	for(const FAssetData& D:Assets)
	{
		TSharedPtr<FJsonObject> Bp=ReadBP(D.GetObjectPathString());
		if(!Bp.IsValid()) continue;
		BPs.Add(Bp); FilesArr.Add(JV(Bp.ToSharedRef()));
		AnalyseBP(Bp,Issues);
	}

	UE_LOG(LogShintTools,Log,TEXT("ShintClient: BP analysis — %d issues from %d BPs"),Issues.Num(),BPs.Num());

	// Return client-side results immediately
	FValidateResult R; R.bOk=true; R.Files=BPs.Num();
	for(const FShintIssue& I:Issues){ R.List.Add(I); if(I.Sev==TEXT("error")) ++R.Errors; else ++R.Warns; ++R.Issues; }
	Done.ExecuteIfBound(R);

	// Fire-and-forget to server for persistence
	if(!FilesArr.IsEmpty())
	{
		TSharedRef<FJsonObject> B=JO(); B->SetArrayField(TEXT("files"),FilesArr); B->SetStringField(TEXT("engine"),TEXT("unreal")); B->SetStringField(TEXT("type"),TEXT("blueprint"));
		Post(C.Base()+TEXT("/validate/blueprints"),ToJson(B), FOnRaw::CreateLambda([](const FShintRaw&){}));
	}
}

// ─── ApplyFixes ───────────────────────────────────────────────────────────────

void FShintClient::ApplyFixes(const TArray<FFixFileRequest>& Reqs, FOnFix Done)
{
	if(Reqs.IsEmpty()){ FFixResult R; R.bIsSuccess=true; Done.ExecuteIfBound(R); return; }
	TArray<TSharedPtr<FJsonValue>> FA;
	for(const FFixFileRequest& Req:Reqs)
	{
		TArray<TSharedPtr<FJsonValue>> IA;
		for(const FAcceptedFix& F:Req.Fixes){ TSharedRef<FJsonObject> IO=JO(); IO->SetStringField(TEXT("rule_id"),F.RuleId); IO->SetNumberField(TEXT("line"),F.Line); IO->SetStringField(TEXT("severity"),TEXT("warning")); IA.Add(JV(IO)); }
		TSharedRef<FJsonObject> FO=JO(); FO->SetStringField(TEXT("file_path"),Req.FilePath); FO->SetStringField(TEXT("content"),Req.Source); FO->SetArrayField(TEXT("issues"),IA);
		FA.Add(JV(FO));
		UE_LOG(LogShintTools,Log,TEXT("ShintClient: ApplyFixes — %d fix(es) for %s"),Req.Fixes.Num(),*FPaths::GetCleanFilename(Req.FilePath));
	}
	TSharedRef<FJsonObject> B=JO(); B->SetArrayField(TEXT("files"),FA);
	Post(C.Base()+TEXT("/validate/fix"),ToJson(B),
		FOnRaw::CreateLambda([Done](const FShintRaw& Raw) mutable {
			FFixResult Result=ParseFix(Raw);
			//if(!Result.bIsSuccess){ UE_LOG(LogShintTools,Error,TEXT("ShintClient: Fix failed — %s"),*Result.Err); Done.ExecuteIfBound(Result); return; }
			for(FFixedFile& FF:Result.Message)
			{
				if(FF.Applied==0||FF.Path.IsEmpty()) continue;
				if(FFileHelper::SaveStringToFile(FF.Content,*FF.Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
				{
					UE_LOG(LogShintTools,Log,TEXT("ShintClient: ✔ Wrote %d fix(es) → %s"),FF.Applied,*FF.Path);
				}
				else
				{
					Result.bIsSuccess=false; UE_LOG(LogShintTools,Error,TEXT("ShintClient: ✘ Write failed → %s"),*FF.Path); }
				}
				{ 
			}
			Done.ExecuteIfBound(Result);
		}));
}

// ─── Dashboard pushes ─────────────────────────────────────────────────────────

void FShintClient::PushCode(const FValidateResult& R, FOnWeb Done)
{
	if(!C.HasDash()){ Done.ExecuteIfBound({false,TEXT("Fill api_key + project_id in Dashboard Config."),TEXT("")}); return; }
	TArray<TSharedPtr<FJsonValue>> FA; TSet<FString> Seen;
	for(const FString& A:R.ScannedPaths){ if(!Seen.Add(A).IsValidId()) continue;
		FString Cnt; FFileHelper::LoadFileToString(Cnt,*A);
		const FString Ext=FPaths::GetExtension(A).ToLower(); int32 Lines=0; for(TCHAR Ch:Cnt) if(Ch=='\n') ++Lines;
		TSharedRef<FJsonObject> FO=JO(); FO->SetStringField(TEXT("name"),FPaths::GetCleanFilename(A)); FO->SetStringField(TEXT("path"),FPaths::GetPath(A));
		FO->SetStringField(TEXT("type"),(Ext==TEXT("h")||Ext==TEXT("hpp"))?TEXT("header"):TEXT("cpp")); FO->SetStringField(TEXT("content"),Cnt); FO->SetNumberField(TEXT("lines_count"),Lines);
		FA.Add(JV(FO)); }
	TSharedRef<FJsonObject> B=JO(); B->SetStringField(TEXT("project_id"),C.ProjectId); B->SetStringField(TEXT("project_name"),C.Name); B->SetStringField(TEXT("api_key"),C.Key); B->SetArrayField(TEXT("files"),FA);
	Post(C.DashUrl/TEXT("api/code-validator/analyze"),ToJson(B), FOnRaw::CreateLambda([Done](const FShintRaw& R) mutable{ Done.ExecuteIfBound({R.bOk,R.Err,R.Body}); }));
}

void FShintClient::PushBlueprints(const TArray<TSharedPtr<FJsonObject>>& Bps, FOnWeb Done)
{
	if(!C.HasDash()){ Done.ExecuteIfBound({false,TEXT("Fill api_key + project_id."),TEXT("")}); return; }
	TArray<TSharedPtr<FJsonValue>> FA; for(const TSharedPtr<FJsonObject>& Bp:Bps) if(Bp.IsValid()) FA.Add(JV(Bp.ToSharedRef()));
	TSharedRef<FJsonObject> B=JO(); B->SetStringField(TEXT("project_id"),C.ProjectId); B->SetStringField(TEXT("project_name"),C.Name); B->SetStringField(TEXT("api_key"),C.Key); B->SetArrayField(TEXT("files"),FA);
	Post(C.DashUrl/TEXT("api/code-validator/analyze"),ToJson(B), FOnRaw::CreateLambda([Done](const FShintRaw& R) mutable{ Done.ExecuteIfBound({R.bOk,R.Err,R.Body}); }));
}

void FShintClient::PushAssets(const FAssetScan& R, FOnWeb Done)
{
	if(!C.HasDash()){ Done.ExecuteIfBound({false,TEXT("Fill api_key + project_id."),TEXT("")}); return; }
	TArray<TSharedPtr<FJsonValue>> IA; for(const FAssetIssue& I:R.List){
		TSharedRef<FJsonObject> O=JO(); O->SetStringField(TEXT("name"),FPaths::GetBaseFilename(I.Path)); O->SetStringField(TEXT("path"),FPaths::GetPath(I.Path)); O->SetStringField(TEXT("type"),TEXT("asset")); O->SetStringField(TEXT("category"),AssetCat(I.Type)); IA.Add(JV(O)); }
	TSharedRef<FJsonObject> B=JO(); B->SetStringField(TEXT("project_id"),C.ProjectId); B->SetStringField(TEXT("project_name"),C.Name); B->SetStringField(TEXT("api_key"),C.Key); B->SetArrayField(TEXT("items"),IA);
	Post(C.DashUrl/TEXT("api/naming-bot/analyze"),ToJson(B), FOnRaw::CreateLambda([Done](const FShintRaw& R) mutable{ Done.ExecuteIfBound({R.bOk,R.Err,R.Body}); }));
}

// ─── Asset Naming ─────────────────────────────────────────────────────────────

void FShintClient::ScanAssets(const FString& ContentDir, FOnAssetScan Done)
{
	TArray<FString> Files; IFileManager::Get().FindFilesRecursive(Files,*ContentDir,TEXT("*.uasset"),true,false);
	TArray<TSharedPtr<FJsonValue>> A;
	for(const FString& P:Files){ FString Rel=P; FPaths::MakePathRelativeTo(Rel,*ContentDir); Rel=Rel.Replace(TEXT("\\"),TEXT("/")); A.Add(JS(TEXT("/Game/")+FPaths::GetBaseFilename(Rel,false))); }
	TSharedRef<FJsonObject> B=JO(); B->SetArrayField(TEXT("asset_paths"),A); B->SetStringField(TEXT("engine"),TEXT("unreal"));
	UE_LOG(LogShintTools,Log,TEXT("ShintClient: ScanAssets — %d assets"),Files.Num());
	Post(C.Base()+TEXT("/assets/scan"),ToJson(B), FOnRaw::CreateLambda([Done](const FShintRaw& Raw) mutable{ Done.ExecuteIfBound(ParseAssets(Raw)); }));
}

void FShintClient::ReportFixes(const TArray<FAssetIssue>& Fixed, FOnAssetFix Done)
{
	TArray<TSharedPtr<FJsonValue>> A; for(const FAssetIssue& I:Fixed){ TSharedRef<FJsonObject> O=JO(); O->SetStringField(TEXT("asset_path"),I.Path); O->SetStringField(TEXT("current_name"),I.Current); O->SetStringField(TEXT("suggested_name"),I.Suggested); O->SetStringField(TEXT("asset_type"),I.Type); A.Add(JV(O)); }
	TSharedRef<FJsonObject> B=JO(); B->SetArrayField(TEXT("issues"),A); const int32 N=Fixed.Num();
	Post(C.Base()+TEXT("/assets/fix"),ToJson(B), FOnRaw::CreateLambda([Done,N](const FShintRaw& R) mutable{ Done.ExecuteIfBound({R.bOk,N,R.bOk?TEXT(""):R.Err}); }));
}

// ─── HTTP ─────────────────────────────────────────────────────────────────────

void FShintClient::Post(const FString& Url, const FString& Body, FOnRaw Done) { Http(Url,TEXT("POST"),Body,Done); }
void FShintClient::Get (const FString& Url, FOnRaw Done)                      { Http(Url,TEXT("GET"), TEXT(""),Done); }

void FShintClient::Http(const FString& Url, const FString& Verb, const FString& Body, FOnRaw Done)
{
	UE_LOG(LogShintTools,Verbose,TEXT("ShintClient: %s %s"),*Verb,*Url);
	TSharedRef<IHttpRequest,ESPMode::ThreadSafe> Req=FHttpModule::Get().CreateRequest();
	Req->SetURL(Url); Req->SetVerb(Verb);
	Req->SetHeader(TEXT("Content-Type"),TEXT("application/json")); Req->SetHeader(TEXT("Accept"),TEXT("application/json")); Req->SetHeader(TEXT("User-Agent"),TEXT("ShintTools-UE5/7.0"));
	if(!Body.IsEmpty()&&Verb==TEXT("POST")) Req->SetContentAsString(Body);
	Req->OnProcessRequestComplete().BindRaw(this,&FShintClient::OnDone,Done); Req->SetTimeout(120.f);
	if(!Req->ProcessRequest()) Done.ExecuteIfBound({false,0,TEXT(""),FString::Printf(TEXT("Dispatch failed → %s"),*Url)});
}

void FShintClient::OnDone(FHttpRequestPtr,FHttpResponsePtr Resp,bool bOk,FOnRaw Done)
{
	FShintRaw R;
	if(!bOk||!Resp.IsValid()){ R.Err=TEXT("Connection failed — Core Engine running?"); Done.ExecuteIfBound(R); return; }
	R.Code=Resp->GetResponseCode(); R.Body=Resp->GetContentAsString(); R.bOk=(R.Code>=200&&R.Code<300);
	if(!R.bOk) R.Err=FString::Printf(TEXT("HTTP %d: %s"),R.Code,*R.Body.Left(200));
	Done.ExecuteIfBound(R);
}

// ─── Parsers ──────────────────────────────────────────────────────────────────

FValidateResult FShintClient::ParseValidate(const FShintRaw& Raw)
{
	FValidateResult R; if(!Raw.bOk){R.Err=Raw.Err;return R;}
	TSharedPtr<FJsonObject> J; TSharedRef<TJsonReader<>> Rd=TJsonReaderFactory<>::Create(Raw.Body);
	if(!FJsonSerializer::Deserialize(Rd,J)||!J.IsValid()){R.Err=TEXT("Parse failed.");return R;}
	R.bOk=true;
	const TSharedPtr<FJsonObject>* S=nullptr;
	if(J->TryGetObjectField(TEXT("summary"),S)&&S){ (*S)->TryGetNumberField(TEXT("total"),R.Issues); (*S)->TryGetNumberField(TEXT("errors"),R.Errors); (*S)->TryGetNumberField(TEXT("warnings"),R.Warns); (*S)->TryGetNumberField(TEXT("files_scanned"),R.Files); }
	const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
	if(J->TryGetArrayField(TEXT("issues"),A)&&A) for(const TSharedPtr<FJsonValue>& V:*A){
		const TSharedPtr<FJsonObject>* O=nullptr; if(!V->TryGetObject(O)||!O) continue;
		FShintIssue I;
		(*O)->TryGetStringField(TEXT("rule_id"),I.Rule); (*O)->TryGetStringField(TEXT("severity"),I.Sev); (*O)->TryGetStringField(TEXT("message"),I.Msg);
		(*O)->TryGetStringField(TEXT("file_path"),I.File); (*O)->TryGetNumberField(TEXT("line"),I.Line);
		(*O)->TryGetStringField(TEXT("snippet"),I.Snippet); (*O)->TryGetStringField(TEXT("fix_suggestion"),I.FixHint);
		(*O)->TryGetBoolField(TEXT("is_auto_fixable"),I.bFixable);
		R.List.Add(MoveTemp(I));
	}
	return R;
}

FAssetScan FShintClient::ParseAssets(const FShintRaw& Raw)
{
	FAssetScan R; if(!Raw.bOk){R.Err=Raw.Err;return R;}
	TSharedPtr<FJsonObject> J; TSharedRef<TJsonReader<>> Rd=TJsonReaderFactory<>::Create(Raw.Body);
	if(!FJsonSerializer::Deserialize(Rd,J)||!J.IsValid()){R.Err=TEXT("Parse failed.");return R;}
	R.bOk=true;
	const TSharedPtr<FJsonObject>* S=nullptr;
	if(J->TryGetObjectField(TEXT("summary"),S)&&S){ (*S)->TryGetNumberField(TEXT("total_assets"),R.Total); (*S)->TryGetNumberField(TEXT("invalid_assets"),R.Invalid); (*S)->TryGetNumberField(TEXT("scan_time_seconds"),R.Secs); }
	const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
	if(J->TryGetArrayField(TEXT("issues"),A)&&A) for(const TSharedPtr<FJsonValue>& V:*A){
		const TSharedPtr<FJsonObject>* O=nullptr; if(!V->TryGetObject(O)||!O) continue;
		FAssetIssue I; (*O)->TryGetStringField(TEXT("asset_path"),I.Path); (*O)->TryGetStringField(TEXT("current_name"),I.Current); (*O)->TryGetStringField(TEXT("suggested_name"),I.Suggested); (*O)->TryGetStringField(TEXT("reason"),I.Reason); (*O)->TryGetStringField(TEXT("asset_type"),I.Type);
		R.List.Add(MoveTemp(I));
	}
	return R;
}

FFixResult FShintClient::ParseFix(const FShintRaw& Raw)
{
	FFixResult R; if(!Raw.bOk){}
	TSharedPtr<FJsonObject> J; TSharedRef<TJsonReader<>> Rd=TJsonReaderFactory<>::Create(Raw.Body);
	if(!FJsonSerializer::Deserialize(Rd,J)||!J.IsValid()){return R;}
	//R.bIsSuccess=true; J->TryGetNumberField(TEXT("total_fixes_applied"),R); J->TryGetNumberField(TEXT("total_fixes_skipped"),R.Skipped);
	const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
	if(J->TryGetArrayField(TEXT("fixed_files"),A)&&A) for(const TSharedPtr<FJsonValue>& V:*A){
		const TSharedPtr<FJsonObject>* O=nullptr; if(!V->TryGetObject(O)||!O) continue;
		FFixedFile FF; (*O)->TryGetStringField(TEXT("file_path"),FF.Path); (*O)->TryGetStringField(TEXT("corrected_content"),FF.Content); (*O)->TryGetNumberField(TEXT("fixes_applied"),FF.Applied); (*O)->TryGetNumberField(TEXT("fixes_skipped"),FF.Skipped);
		//R.Message.ad(MoveTemp(FF));
	}
	return R;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

void FShintClient::CollectCpp(const FString& Dir, TArray<FString>& Out)
{
	TArray<FString> A,B; IFileManager::Get().FindFilesRecursive(A,*Dir,TEXT("*.cpp"),true,false); IFileManager::Get().FindFilesRecursive(B,*Dir,TEXT("*.h"),true,false); Out.Append(A); Out.Append(B);
}

FString FShintClient::AssetCat(const FString& T)
{
	if(T.Contains(TEXT("Texture")))  return TEXT("texture");
	if(T.Contains(TEXT("Mesh")))     return TEXT("mesh");
	if(T.Contains(TEXT("Material"))) return TEXT("material");
	if(T.Contains(TEXT("Blueprint"))||T.Contains(TEXT("Widget"))) return TEXT("blueprint");
	if(T.Contains(TEXT("Sound")))    return TEXT("audio");
	if(T.Contains(TEXT("Anim")))     return TEXT("animation");
	return TEXT("asset");
}
