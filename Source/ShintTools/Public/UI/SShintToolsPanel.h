// Copyright 2026 ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ShintCoreClient.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

class FCoreProcessManager;

enum class ECoreStatus  : uint8 { Unknown, Online, Offline, Checking };
enum class EModuleState : uint8 { Idle, Running, Done, Error };

enum class EIssueFilter : uint8 { All, FixableOnly };

enum class EIssueCategoryFilter : uint8
{
	All, Performance, BestPractices, Security, Maintainability
};

enum class EIssueSeverityFilter : uint8
{
	All, Critical, Error, Warning, Info
};

enum class EAssetTypeFilter : uint8
{
	All, Materials, Textures, Meshes, Blueprints, VFX, Audio, Animations, Data
};

enum class ECodeTypeFilter : uint8 { All, CppOnly, BlueprintsOnly };

struct FShintIssueItem
{
	FString RuleId;
	FString Severity;
	FString Message;
	FString FilePath;
	FString FileName;
	int32   Line           = 0;
	FString Snippet;
	FString FixSuggestion;
	bool    bIsAutoFixable = false;
	bool    bChecked       = false;
	bool    bIsBlueprint   = false;
	int32   OriginalIndex  = -1;

	FString Class;
	FString Category;
	FString Graph;

	FString RuleName;
	FString RuleExplanation;

	FString FileContent;

	FString ContextBefore;
	FString ContextAfter;
	int32   ContextLineStart = 0;
	bool    bPreviewExpanded = false;

	FString FixPreviewCode;
	bool    bFixPreviewLoading = false;
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

class SShintToolsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintToolsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SShintToolsPanel() override;

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

	static FSlateFontInfo F_Title()   { return FCoreStyle::GetDefaultFontStyle("Bold",    18); }
	static FSlateFontInfo F_Body()    { return FCoreStyle::GetDefaultFontStyle("Regular", 12); }
	static FSlateFontInfo F_Small()   { return FCoreStyle::GetDefaultFontStyle("Regular", 11); }
	static FSlateFontInfo F_Label()   { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
	static FSlateFontInfo F_Mono()    { return FCoreStyle::GetDefaultFontStyle("Mono",    10); }

private:

	TSharedRef<SWidget> BuildOverviewHero();
	TSharedRef<SWidget> BuildConfigSection();
	TSharedRef<SWidget> BuildCodeValidatorSection();
	TSharedRef<SWidget> BuildCodeResultsPanel();
	TSharedRef<SWidget> BuildCodeFilterBar();
	TSharedRef<SWidget> BuildCategoryMenuContent();
	TSharedRef<SWidget> BuildSeverityMenuContent();

	TSharedRef<SWidget> BuildAssetNamingSection();
	TSharedRef<SWidget> BuildAssetResultsPanel();
	TSharedRef<SWidget> BuildAssetTypeMenuContent();

	TSharedRef<ITableRow> GenerateCodeIssueRow(
		FShintIssueItemPtr Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> GenerateAssetIssueRow(
		FShintAssetItemPtr Item, const TSharedRef<STableViewBase>& Owner);

	FReply OnCheckConnectionClicked();
	FReply OnScanProjectClicked();
	FReply OnScanBlueprintsClicked();
	FReply OnSelectAllCodeClicked();
	FReply OnDeselectAllCodeClicked();
	FReply OnApplySelectedCodeFixesClicked();

	FReply OnExplainIssueClicked(FShintIssueItemPtr Item);
	FReply OnScanAssetsClicked();
	FReply OnApplySingleFix(FShintIssueItemPtr Item);
	FReply OnIgnoreSingleFix(FShintIssueItemPtr Item);
	void   FetchFixPreview(FShintIssueItemPtr Item);
	void   OnSafetyCheckComplete(const FShintSafetyCheckResult& Result);
	void   ShowSafetyWarningDialog(const FShintSafetyCheckResult& Result);

	void   ShowSafetyUnavailableDialog();
	void   ProceedWithCodeFixes();
	FReply OnSelectAllAssetsClicked();
	FReply OnDeselectAllAssetsClicked();
	FReply OnApplySelectedAssetFixesClicked();

	FReply OnAssetRowNavigateClicked(FShintAssetItemPtr Item);

	void OnHealthCheckComplete(const FShintRequestResult& Result);
	void OnProjectValidateComplete(const FShintValidateResult& Result);
	void OnBlueprintValidateComplete(const FShintValidateResult& Result);
	void OnBlueprintNamingScanComplete(const FShintValidateResult& Result);
	void OnCodeFixComplete(const FShintFixResult& Result, uint32 FixGeneration);
	void OnAssetScanComplete(const FShintAssetScanResult& Result);

	void OnAssetScanFromBPComplete(const FShintAssetScanResult& Result);
	void OnAssetFixComplete(const FShintAssetFixResult& Result);

	void SetStatus(ECoreStatus S);
	void SetCodeState(EModuleState S);
	void SetAssetState(EModuleState S);
	void PopulateCodeIssueList(const FShintValidateResult& Result, bool bIsBPScan);
	void PopulateAssetIssueList(const FShintAssetScanResult& Result);
	void ApplyCodeFilter();
	void ApplyAssetFilter();
	void RefreshCodeStats();
	void RefreshAssetStats();

	void RefreshQualityScore();
	void OnLatestScoreFetched(const FShintQualityScoreSnapshot& Snap);
	void RefreshApplyCodeLabel();
	void RefreshApplyAssetLabel();
	void SaveConfigOverrides();

	FSlateColor GetStatusColor()        const;
	FText       GetStatusText()         const;
	TOptional<float> GetCodeProgress()  const;
	TOptional<float> GetAssetProgress() const;

	void HandleValidateResult(const FShintValidateResult& Result, bool bMerge, bool bIsBPScan);

	static FString FmtN(int32 N);
	static TSharedRef<SWidget> Divider();
	static TSharedRef<SWidget> BuildSectionTitle(const FText& Title, const FText& Subtitle);
	static TSharedRef<SWidget> BuildDiffLine(const FString& Icon, const FString& Text,
		const FLinearColor& IconColor, const FLinearColor& TextColor);
	static TSharedRef<SWidget> BuildContextPanel(
		const FString& Label, const FString& ContextText,
		int32 ContextLineStart, int32 IssueLineNo,
		const FLinearColor& HighlightColor);
	static TSharedRef<SWidget> BuildModuleProgressBar(
		TSharedPtr<SProgressBar>& OutBar,
		TAttribute<TOptional<float>> PercentAttr);

	TSharedPtr<FShintCoreClient>     CoreClient;

	TSharedPtr<FCoreProcessManager> ProcessManager;

	ECoreStatus  StatusState = ECoreStatus::Unknown;
	EModuleState CodeState   = EModuleState::Idle;
	EModuleState AssetState  = EModuleState::Idle;

	FShintValidateResult  LastCodeResult;
	FShintAssetScanResult LastAssetResult;
	FShintQualityScoreSnapshot LastQualityScore;

	TArray<FShintIssueItemPtr> AllCodeItems;
	TArray<FShintAssetItemPtr> AllAssetItems;

	TArray<FShintIssueItemPtr> CodeIssueItems;
	TArray<FShintAssetItemPtr> AssetIssueItems;

	EIssueFilter         CurrentFilter            = EIssueFilter::All;
	EIssueCategoryFilter CurrentCategoryFilter    = EIssueCategoryFilter::All;
	EIssueSeverityFilter CurrentSeverityFilter    = EIssueSeverityFilter::All;
	EAssetTypeFilter     CurrentAssetTypeFilter   = EAssetTypeFilter::All;
	ECodeTypeFilter      CurrentCodeTypeFilter    = ECodeTypeFilter::All;

	FString              CodeSearchText;
	FString              AssetSearchText;

	TSet<FString>           AppliedFixFingerprints;
	TArray<FShintCodeIssue> PendingCodeFixes;

	uint32 ScanGeneration      = 0;

	bool   bBlueprintScanActive = false;

	TSharedPtr<SListView<FShintIssueItemPtr>> CodeIssueListView;
	TSharedPtr<SListView<FShintAssetItemPtr>> AssetIssueListView;

	TSharedPtr<STextBlock> CodeFiles_Label;
	TSharedPtr<STextBlock> CodeErrors_Label;
	TSharedPtr<STextBlock> CodeWarnings_Label;
	TSharedPtr<STextBlock> CodeScore_Label;
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
	TSharedPtr<STextBlock> SendCodeBtnLabel;
	TSharedPtr<STextBlock> SendAssetBtnLabel;

	TSharedPtr<STextBlock> CategoryFilterLabel;
	TSharedPtr<STextBlock> SeverityFilterLabel;
	TSharedPtr<STextBlock> AssetTypeFilterLabel;

	TSharedPtr<SWidget>    CodeEmptyState;
	TSharedPtr<SWidget>    AssetEmptyState;
	TSharedPtr<STextBlock> CodeEmptyText;
	TSharedPtr<STextBlock> AssetEmptyText;
	int32                  AssetFixesApplied = 0;

	TSharedPtr<SEditableTextBox> ApiKeyDashboardField;
	TSharedPtr<SEditableTextBox> DashboardUrlField;

	TSharedPtr<SEditableTextBox> CorePortField;
	TSharedPtr<SEditableTextBox> ApiKeyMongoField;
	TSharedPtr<class SMultiLineEditableTextBox> ExcludedPathsField;
	TSharedPtr<SEditableTextBox> ExportPathField;

	int32                             CurrentDestinationIndex = 0;
	int32                             CurrentConnStateIndex   = 0;
	TSharedPtr<class SWidgetSwitcher> DestinationSwitcher;

	void SetDestinationIndex(int32 Index);
};
