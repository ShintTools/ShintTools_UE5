// Copyright ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ShintCoreClient.h"
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

class FCoreProcessManager;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

enum class ECoreStatus  : uint8 { Unknown, Online, Offline, Checking };
enum class EModuleState : uint8 { Idle, Running, Done, Error };
enum class EIssueFilter : uint8 { All, ErrorsOnly, WarningsOnly, FixableOnly };

// ─────────────────────────────────────────────────────────────────────────────
// List item types (shared_ptr owned by TArray for SListView)
// ─────────────────────────────────────────────────────────────────────────────

struct FShintIssueItem
{
	FString RuleId;
	FString Severity;
	FString Message;
	FString FilePath;
	FString FileName;        // cached for display
	int32   Line           = 0;
	FString Snippet;
	FString FixSuggestion;
	bool    bIsAutoFixable = false;
	bool    bChecked       = false;
	int32   OriginalIndex  = -1;
};
using FShintIssueItemPtr = TSharedPtr<FShintIssueItem>;

struct FShintAssetItem
{
	FString AssetPath;
	FString CurrentName;
	FString SuggestedName;
	FString Reason;
	FString AssetType;
	bool    bChecked      = true;
	int32   OriginalIndex = -1;
};
using FShintAssetItemPtr = TSharedPtr<FShintAssetItem>;

// ─────────────────────────────────────────────────────────────────────────────
// Panel widget
// ─────────────────────────────────────────────────────────────────────────────

class SShintToolsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintToolsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SShintToolsPanel() override;

	// ── Brand palette ────────────────────────────────────────────────────────
	static FLinearColor C_BG()       { return FLinearColor(0.000f, 0.000f, 0.000f, 1.f); }
	static FLinearColor C_Surface()  { return FLinearColor(0.048f, 0.048f, 0.048f, 1.f); }
	static FLinearColor C_Border()   { return FLinearColor(0.110f, 0.110f, 0.110f, 1.f); }
	static FLinearColor C_White()    { return FLinearColor(1.000f, 1.000f, 1.000f, 1.f); }
	static FLinearColor C_Gray()     { return FLinearColor(0.560f, 0.560f, 0.560f, 1.f); }
	static FLinearColor C_DimGray()  { return FLinearColor(0.300f, 0.300f, 0.300f, 1.f); }
	static FLinearColor C_Blue()     { return FLinearColor(0.145f, 0.430f, 0.940f, 1.f); }
	static FLinearColor C_Green()    { return FLinearColor(0.145f, 0.820f, 0.380f, 1.f); }
	static FLinearColor C_Red()      { return FLinearColor(0.940f, 0.200f, 0.200f, 1.f); }
	static FLinearColor C_Yellow()   { return FLinearColor(1.000f, 0.780f, 0.000f, 1.f); }
	static FLinearColor C_RowEven()  { return FLinearColor(0.038f, 0.038f, 0.038f, 1.f); }
	static FLinearColor C_RowOdd()   { return FLinearColor(0.018f, 0.018f, 0.018f, 1.f); }
	static FLinearColor C_CodeBG()   { return FLinearColor(0.055f, 0.055f, 0.055f, 1.f); }
	static FLinearColor C_DiffRed()  { return FLinearColor(0.900f, 0.480f, 0.480f, 1.f); }
	static FLinearColor C_DiffGreen(){ return FLinearColor(0.480f, 0.900f, 0.480f, 1.f); }

	// ── Fonts ────────────────────────────────────────────────────────────────
	static FSlateFontInfo F_Title()   { return FCoreStyle::GetDefaultFontStyle("Bold",    18); }
	static FSlateFontInfo F_H2()      { return FCoreStyle::GetDefaultFontStyle("Bold",    13); }
	static FSlateFontInfo F_Body()    { return FCoreStyle::GetDefaultFontStyle("Regular", 12); }
	static FSlateFontInfo F_Small()   { return FCoreStyle::GetDefaultFontStyle("Regular", 11); }
	static FSlateFontInfo F_Label()   { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
	static FSlateFontInfo F_Mono()    { return FCoreStyle::GetDefaultFontStyle("Mono",    10); }
	static FSlateFontInfo F_RuleId()  { return FCoreStyle::GetDefaultFontStyle("Bold",    11); }
	static FSlateFontInfo F_StatNum() { return FCoreStyle::GetDefaultFontStyle("Bold",    24); }
	static FSlateFontInfo F_StatCap() { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }

private:
	// ── Widget builders ───────────────────────────────────────────────────────
	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildConfigSection();
	TSharedRef<SWidget> BuildStatusBar();
	TSharedRef<SWidget> BuildCodeValidatorSection();
	TSharedRef<SWidget> BuildCodeResultsPanel();
	TSharedRef<SWidget> BuildCodeFilterBar();
	TSharedRef<SWidget> BuildAssetNamingSection();
	TSharedRef<SWidget> BuildAssetResultsPanel();

	// ── Row generators for SListView ──────────────────────────────────────────
	TSharedRef<ITableRow> GenerateCodeIssueRow(
		FShintIssueItemPtr Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> GenerateAssetIssueRow(
		FShintAssetItemPtr Item, const TSharedRef<STableViewBase>& Owner);

	// ── Button handlers ───────────────────────────────────────────────────────
	FReply OnCheckConnectionClicked();
	FReply OnScanProjectClicked();
	FReply OnScanBlueprintsClicked();
	FReply OnSelectAllCodeClicked();
	FReply OnDeselectAllCodeClicked();
	FReply OnApplySelectedCodeFixesClicked();
	FReply OnSendCodeToDashboardClicked();
	FReply OnScanAssetsClicked();
	FReply OnSelectAllAssetsClicked();
	FReply OnApplySelectedAssetFixesClicked();
	FReply OnSendAssetToDashboardClicked();

	// ── HTTP callbacks ────────────────────────────────────────────────────────
	void OnHealthCheckComplete(const FShintRequestResult& Result);
	void OnProjectValidateComplete(const FShintValidateResult& Result);
	void OnBlueprintValidateComplete(const FShintValidateResult& Result);
	void OnCodeFixComplete(const FShintFixResult& Result);
	void OnCodeDashboardComplete(const FShintWebDashboardResult& Result);
	void OnAssetScanComplete(const FShintAssetScanResult& Result);
	void OnAssetFixComplete(const FShintAssetFixResult& Result);
	void OnAssetDashboardComplete(const FShintWebDashboardResult& Result);

	// ── UI state helpers ──────────────────────────────────────────────────────
	void SetStatus(ECoreStatus S);
	void SetCodeState(EModuleState S);
	void SetAssetState(EModuleState S);
	void PopulateCodeIssueList(const FShintValidateResult& Result);
	void PopulateAssetIssueList(const FShintAssetScanResult& Result);
	void ApplyCodeFilter();
	void RefreshCodeStats();
	void RefreshAssetStats();
	void RefreshApplyCodeLabel();
	void RefreshApplyAssetLabel();
	void SaveConfigOverrides();

	FSlateColor GetStatusColor()        const;
	FText       GetStatusText()         const;
	TOptional<float> GetCodeProgress()  const;
	TOptional<float> GetAssetProgress() const;

	void HandleValidateResult(const FShintValidateResult& Result, bool bMerge);

	static FString FmtN(int32 N);
	static TSharedRef<SWidget> Divider();
	static TSharedRef<SWidget> BuildSectionTitle(const FText& Title, const FText& Subtitle);
	static TSharedRef<SWidget> BuildDiffLine(const FString& Icon, const FString& Text,
		const FLinearColor& IconColor, const FLinearColor& TextColor);
	static TSharedRef<SWidget> BuildModuleProgressBar(
		TSharedPtr<SProgressBar>& OutBar,
		TAttribute<TOptional<float>> PercentAttr);

	// ── State ─────────────────────────────────────────────────────────────────
	TSharedPtr<FShintCoreClient>    CoreClient;
	TSharedPtr<FCoreProcessManager> ProcessManager;

	ECoreStatus  StatusState = ECoreStatus::Unknown;
	EModuleState CodeState   = EModuleState::Idle;
	EModuleState AssetState  = EModuleState::Idle;

	FShintValidateResult  LastCodeResult;
	FShintAssetScanResult LastAssetResult;

	// All issues from last scan
	TArray<FShintIssueItemPtr> AllCodeItems;
	// Currently visible (after filter)
	TArray<FShintIssueItemPtr> CodeIssueItems;
	TArray<FShintAssetItemPtr> AssetIssueItems;

	EIssueFilter CurrentFilter = EIssueFilter::All;

	// ── Slate refs ────────────────────────────────────────────────────────────
	TSharedPtr<SListView<FShintIssueItemPtr>> CodeIssueListView;
	TSharedPtr<SListView<FShintAssetItemPtr>> AssetIssueListView;

	TSharedPtr<STextBlock> CodeFiles_Label;
	TSharedPtr<STextBlock> CodeErrors_Label;
	TSharedPtr<STextBlock> CodeWarnings_Label;
	TSharedPtr<STextBlock> AssetTotal_Label;
	TSharedPtr<STextBlock> AssetInvalid_Label;
	TSharedPtr<STextBlock> AssetTime_Label;

	TSharedPtr<SProgressBar> CodeProgressBar;
	TSharedPtr<SProgressBar> AssetProgressBar;

	TSharedPtr<SButton>    ApplyCodeBtn;
	TSharedPtr<SButton>    SendCodeBtn;
	TSharedPtr<SButton>    ApplyAssetBtn;
	TSharedPtr<SButton>    SendAssetBtn;

	TSharedPtr<STextBlock> ApplyCodeBtnLabel;
	TSharedPtr<STextBlock> ApplyAssetBtnLabel;

	TSharedPtr<SWidget>    CodeEmptyState;
	TSharedPtr<SWidget>    AssetEmptyState;

	// Config field widgets
	TSharedPtr<SEditableTextBox> ProjectIdField;
	TSharedPtr<SEditableTextBox> ApiKeyField;
	TSharedPtr<SEditableTextBox> DashboardUrlField;
};
