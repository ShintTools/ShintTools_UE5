// Copyright ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// ── Include the client header directly — this resolves ALL struct types       ──
// ── (FShintRaw, FValidateResult, FFixResult, FAssetScan, FAssetFix, FWebResult) ──
// ── No forward declarations of structs needed — they are fully defined here.  ──
#include "ShintCoreClient.h"

// Slate
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

// Asset tools (for IAssetTools::RenameAssets)
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// Forward-declare only classes (not structs — those are in ShintCoreClient.h)
class FCoreProcessManager;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

enum class EStatus : uint8 { Unknown, Online, Offline, Checking };
enum class EModule : uint8 { Idle, Running, Done, Err };
enum class EFilter : uint8 { All, Errors, Warnings, Fixable };

// ─────────────────────────────────────────────────────────────────────────────
// Row item structs (UI-only, not part of the client API)
// ─────────────────────────────────────────────────────────────────────────────

// Code issue row item
struct FCodeItem
{
	FString Rule, Sev, Msg, File, FileName, Snippet, FixHint;
	int32   Line     = 0;
	bool    bFixable = false;
	bool    bChecked = false;   // plain bool; RebuildList reads fresh value each time
	int32   Idx      = -1;
};
using FCodePtr = TSharedPtr<FCodeItem>;

// Asset naming row item
struct FAssetItem
{
	FString Path, Current, Suggested, Reason, Type;
	bool    bChecked = true;
	int32   Idx      = -1;
};
using FAssetPtr = TSharedPtr<FAssetItem>;

// ─────────────────────────────────────────────────────────────────────────────
// Panel
// ─────────────────────────────────────────────────────────────────────────────

class SShintToolsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintToolsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SShintToolsPanel() override;

	// ── Palette ───────────────────────────────────────────────────────────────
	static FLinearColor BG()      { return {0.f,    0.f,    0.f,    1.f}; }
	static FLinearColor Surf()    { return {0.048f, 0.048f, 0.048f, 1.f}; }
	static FLinearColor Brd()     { return {0.11f,  0.11f,  0.11f,  1.f}; }
	static FLinearColor White()   { return {1.f,    1.f,    1.f,    1.f}; }
	static FLinearColor Gray()    { return {0.56f,  0.56f,  0.56f,  1.f}; }
	static FLinearColor Dim()     { return {0.30f,  0.30f,  0.30f,  1.f}; }
	static FLinearColor Blue()    { return {0.145f, 0.43f,  0.94f,  1.f}; }
	static FLinearColor Green()   { return {0.145f, 0.82f,  0.38f,  1.f}; }
	static FLinearColor Red()     { return {0.94f,  0.20f,  0.20f,  1.f}; }
	static FLinearColor Yellow()  { return {1.f,    0.78f,  0.f,    1.f}; }
	static FLinearColor EvenRow() { return {0.038f, 0.038f, 0.038f, 1.f}; }
	static FLinearColor OddRow()  { return {0.018f, 0.018f, 0.018f, 1.f}; }

	// ── Fonts ─────────────────────────────────────────────────────────────────
	static FSlateFontInfo FT()  { return FCoreStyle::GetDefaultFontStyle("Bold",    18); }
	static FSlateFontInfo FH()  { return FCoreStyle::GetDefaultFontStyle("Bold",    13); }
	static FSlateFontInfo FS()  { return FCoreStyle::GetDefaultFontStyle("Regular", 11); }
	static FSlateFontInfo FL()  { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
	static FSlateFontInfo FM()  { return FCoreStyle::GetDefaultFontStyle("Mono",    10); }
	static FSlateFontInfo FB()  { return FCoreStyle::GetDefaultFontStyle("Bold",    11); }
	static FSlateFontInfo FSN() { return FCoreStyle::GetDefaultFontStyle("Bold",    24); }
	static FSlateFontInfo FSC() { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }

private:
	// ── Widget builders ───────────────────────────────────────────────────────
	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildCfg();
	TSharedRef<SWidget> BuildStatus();
	TSharedRef<SWidget> BuildCode();
	TSharedRef<SWidget> BuildCodeList();
	TSharedRef<SWidget> BuildAssets();
	TSharedRef<SWidget> BuildAssetList();

	// ── Row generators ────────────────────────────────────────────────────────
	TSharedRef<ITableRow> CodeRow (FCodePtr  Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> AssetRow(FAssetPtr Item, const TSharedRef<STableViewBase>& Owner);

	// ── Button handlers ───────────────────────────────────────────────────────
	FReply OnHealth();
	FReply OnScanSrc();
	FReply OnScanBP();
	FReply OnSelAll();
	FReply OnDeselAll();
	FReply OnApply();
	FReply OnPushCpp();
	FReply OnPushBP();
	FReply OnScanAssets();
	FReply OnSelAllAssets();
	FReply OnApplyAssets();
	FReply OnPushAssets();

	// ── HTTP callbacks ────────────────────────────────────────────────────────
	void OnHealthDone   (const FShintRaw&     Result);
	void OnCodeDone     (const FValidateResult& Result);
	void OnBpDone       (const FValidateResult& Result);
	void OnFixDone      (const FFixResult&    Result);
	void OnCppPushDone  (const FWebResult&    Result);
	void OnBpPushDone   (const FWebResult&    Result);
	void OnAssetsDone   (const FAssetScan&    Result);
	void OnAssetFixDone (const FAssetFix&     Result);
	void OnAssetPushDone(const FWebResult&    Result);

	// ── UI helpers ────────────────────────────────────────────────────────────
	void SetSt   (EStatus S);
	void SetCode (EModule S);
	void SetAsset(EModule S);
	void ApplyFilter();
	void RefCodeStats();
	void RefAssetStats();
	void RefApplyBtn();
	void RefAssetApplyBtn();
	void FlushCfg();

	FSlateColor      StatusColor() const;
	FText            StatusText()  const;
	TOptional<float> CodePct()     const;
	TOptional<float> AssetPct()    const;

	static TSharedRef<SWidget> Div();
	static FString             N(int32 V);

	// ── State ─────────────────────────────────────────────────────────────────
	TSharedPtr<FShintClient>        Client;
	TSharedPtr<FCoreProcessManager> Proc;

	EStatus St      = EStatus::Unknown;
	EModule CodeSt  = EModule::Idle;
	EModule AssetSt = EModule::Idle;
	EFilter Flt     = EFilter::All;

	FValidateResult LastCode;    // accumulated result for dashboard push
	FAssetScan      LastAsset;

	TArray<FCodePtr>  AllCode;   // all issues (C++ + BP)
	TArray<FCodePtr>  ViewCode;  // filtered subset
	TArray<FAssetPtr> AllAssets;

	// ── Slate widget refs ─────────────────────────────────────────────────────
	TSharedPtr<SListView<FCodePtr>>  CodeList;
	TSharedPtr<SListView<FAssetPtr>> AssetList;

	TSharedPtr<SEditableTextBox> BxProjId, BxKey, BxName;
	TSharedPtr<STextBlock>       LFiles, LErrCode, LWarnCode;
	TSharedPtr<STextBlock>       LAssets, LInvalid, LTime;
	TSharedPtr<SProgressBar>     PbCode, PbAsset;
	TSharedPtr<SButton>          BtnApply, BtnCpp, BtnBp;
	TSharedPtr<SButton>          BtnApplyAsset, BtnPushAsset;
	TSharedPtr<STextBlock>       LApply, LApplyAsset;
	TSharedPtr<SWidget>          EmptyCode, EmptyAsset;
};
