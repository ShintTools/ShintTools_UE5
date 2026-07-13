// Copyright 2026 ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ShintCoreClient.h"
// [DASH-STRIP-BEGIN]
// FShintWebDashboardResult / FOnShintWebDashboardComplete used by the
// OnCodeDashboardComplete / OnAssetDashboardComplete signatures below.
// These types moved out of ShintCoreClient.h in the dashboard-sync
// refactor.
#include "ShintDashboardSync.h"
// [DASH-STRIP-END]
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Containers/Ticker.h"          // FTSTicker — drives Explain modal status rotation

class FCoreProcessManager;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

enum class ECoreStatus  : uint8 { Unknown, Online, Offline, Checking };
enum class EModuleState : uint8 { Idle, Running, Done, Error };

// Legacy – kept only so ApplyCodeFilter can gate on Fixable
enum class EIssueFilter : uint8 { All, FixableOnly };

// Category filter – matches web dashboard dropdown
enum class EIssueCategoryFilter : uint8
{
	All, Performance, BestPractices, Security, Maintainability
};

// Severity filter – matches web dashboard dropdown
enum class EIssueSeverityFilter : uint8
{
	All, Critical, Error, Warning, Info
};

// Asset type filter for the naming bot panel
enum class EAssetTypeFilter : uint8
{
	All, Materials, Textures, Meshes, Blueprints, VFX, Audio, Animations, Data
};

// Code type filter — separates C++ rules from Blueprint rules in the Deep Code Validator
enum class ECodeTypeFilter : uint8 { All, CppOnly, BlueprintsOnly };

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
	bool    bIsBlueprint   = false;  // true when issued by ValidateBlueprints
	int32   OriginalIndex  = -1;

	// Extended fields
	FString Class;
	FString Category;
	FString Graph;

	// LLM pivot — humanised label + docstring rationale. Forwarded as-is to
	// /agent/explain when the user clicks the per-row "Explain" button.
	FString RuleName;
	FString RuleExplanation;

	// Full source file content (from server) — for tree-sitter AST fix validation
	FString FileContent;

	// Before/after diff context (from server)
	FString ContextBefore;
	FString ContextAfter;       // local fallback: fix_suggestion substituted into window
	int32   ContextLineStart = 0;
	bool    bPreviewExpanded = false;

	// Tree-sitter fix preview: fetched on-demand from /validate/fix
	FString FixPreviewCode;     // extracted window from fixed_code — shown in AFTER panel
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

// [LOD-STRIP-BEGIN]
// One LOD audit finding row. Wraps FShintLodFinding (the client/transport
// struct) with display-only state. Guidance + AiGuidance are rendered in an
// expandable detail block under the row.
struct FShintLodFindingItem
{
	FShintLodFinding Finding;     // server result, copied verbatim
	bool bDetailExpanded = false; // user toggled the guidance panel open
	bool bChecked        = false; // row checkbox — drives the bulk "Fix (N)"
	// Kept alive for the lifetime of the row so the thumbnail widget it backs
	// stays valid (FAssetThumbnail must outlive the widget MakeThumbnailWidget
	// returns). Created lazily in GenerateLodFindingRow.
	TSharedPtr<class FAssetThumbnail> Thumbnail;
};
using FShintLodFindingPtr = TSharedPtr<FShintLodFindingItem>;

// Asset Optimizer result tab — findings are grouped by asset family.
enum class ELodTab : uint8 { Textures, Meshes, Materials };
// [LOD-STRIP-END]

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
	static FSlateFontInfo F_Body()    { return FCoreStyle::GetDefaultFontStyle("Regular", 12); }
	static FSlateFontInfo F_Small()   { return FCoreStyle::GetDefaultFontStyle("Regular", 11); }
	static FSlateFontInfo F_Label()   { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
	static FSlateFontInfo F_Mono()    { return FCoreStyle::GetDefaultFontStyle("Mono",    10); }
	// CLEANUP — F_StatNum / F_StatCap removed (0 callers after v1.2.14 KPI
	// rewrite). F_RuleId / F_H2 also dropped — single callers migrated to
	// FShintStyle::Fonts::Small() / H2() in the .cpp.

private:
	// ── Widget builders ───────────────────────────────────────────────────────
	// BuildHeader / BuildStatusBar were retired with the UI-REDESIGN switch to
	// the SShintTopBar/SShintSidebar shell — neither was reachable from the
	// destination switcher, so they were removed entirely instead of carried
	// forward as dead overloads.
	TSharedRef<SWidget> BuildOverviewHero();   // 4-up KPI hero for the Overview destination
	TSharedRef<SWidget> BuildConfigSection();
	TSharedRef<SWidget> BuildCodeValidatorSection();
	TSharedRef<SWidget> BuildCodeResultsPanel();
	TSharedRef<SWidget> BuildCodeFilterBar();
	TSharedRef<SWidget> BuildCategoryMenuContent();
	TSharedRef<SWidget> BuildSeverityMenuContent();
	// BuildCodeTypeMenuContent retired — the C++/Blueprints choice is the
	// LOD-design tab strip inside BuildCodeFilterBar.
	TSharedRef<SWidget> BuildAssetNamingSection();
	TSharedRef<SWidget> BuildAssetResultsPanel();
	TSharedRef<SWidget> BuildAssetTypeMenuContent();

	// [LOD-STRIP-BEGIN]
	// ── LOD Auditor / Asset Optimizer (Studio tier) ───────────────────────────
	TSharedRef<SWidget> BuildLodAuditSection();
	TSharedRef<SWidget> BuildLodKpiRow();
	TSharedRef<SWidget> BuildLodToolbar();      // tabs + filters + bulk actions
	TSharedRef<SWidget> BuildLodResultsPanel();
	TSharedRef<SWidget> BuildLodTableHeader();
	TSharedRef<ITableRow> GenerateLodFindingRow(
		FShintLodFindingPtr Item, const TSharedRef<STableViewBase>& Owner);
	// [LOD-STRIP-END]

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
	// [DASH-STRIP-BEGIN]
	FReply OnSendCodeToDashboardClicked();
	// [DASH-STRIP-END]
	// OnAutoFixPlanClicked / OnAgentPlanComplete / ShowAgentPlanDialog
	// were removed in 1.7.11 alongside the Auto-Fix Plan button. The
	// per-row Explain entry point covers the same UX with focused
	// /agent/explain context.

	// [AGENT-STRIP-BEGIN]
	// LLM pivot — single-shot /agent/explain modal.
	// One per-issue "Explain" button on each row; click opens the modal,
	// the request fires, the server takes 30-45s on CPU and the modal shows
	// a spinner with rotating status text until the response arrives.
	FReply OnExplainIssueClicked(FShintIssueItemPtr Item);
	// Live token from /agent/explain/stream — appended to the modal as it
	// arrives. RequestId guards against a stale stream writing into a modal that
	// now shows a different issue.
	void   OnExplainChunk(const FString& Chunk, uint64 RequestId);
	// Accumulates streamed tokens for the currently-open modal.
	FString ExplainStreamBuffer;
	void   OnExplainComplete(const FShintAgentExplainResponse& Result,
	                         FShintIssueItemPtr                Item,
	                         uint64                            RequestId);
	bool   TickExplainStatus(float DeltaTime); // rotates ExplainStatusIndex
	TSharedPtr<class SWindow>                         ExplainWindow;
	TSharedPtr<class SMultiLineEditableTextBox>       ExplainResultBox;
	TSharedPtr<class SCircularThrobber>               ExplainSpinner;
	TSharedPtr<class STextBlock>                      ExplainStatusLine;
	int32                                             ExplainStatusIndex = 0;
	// Monotonic token for the in-flight explain request. Bumped on every
	// click; OnExplainComplete ignores any response whose token is stale, so
	// a slow answer for issue A can't write into the modal now showing issue
	// B (the per-row "wrong explanation" race).
	uint64                                            ExplainRequestId = 0;
	FTSTicker::FDelegateHandle                        ExplainTickerHandle;
	// [AGENT-STRIP-END]
	FReply OnScanAssetsClicked();
	FReply OnApplySingleFix(FShintIssueItemPtr Item);
	FReply OnIgnoreSingleFix(FShintIssueItemPtr Item);
	void   FetchFixPreview(FShintIssueItemPtr Item);
	void   OnSafetyCheckComplete(const FShintSafetyCheckResult& Result);
	void   ShowSafetyWarningDialog(const FShintSafetyCheckResult& Result);
	void   ProceedWithCodeFixes();
	FReply OnSelectAllAssetsClicked();
	FReply OnDeselectAllAssetsClicked();   // T6
	FReply OnApplySelectedAssetFixesClicked();
	// [DASH-STRIP-BEGIN]
	FReply OnSendAssetToDashboardClicked();
	// [DASH-STRIP-END]

	// [LOD-STRIP-BEGIN]
	// ── LOD Auditor handlers ──────────────────────────────────────────────────
	FReply OnAuditLodsClicked();
	void   OnLodAuditComplete(const FShintLodAuditResult& Result);
	void   PopulateLodFindingList(const FShintLodAuditResult& Result);
	void   RefreshLodStats();
	void   RefreshLodFilteredList();        // re-apply tab + filters → visible rows
	void   SetLodTab(ELodTab Tab);
	int32  LodCheckedCount() const;         // selected rows (bulk Fix label)
	void   SetLodAllChecked(bool bChecked); // Select All / Deselect All (current tab)
	FReply OnLodFixRow(FShintLodFindingPtr Item);
	FReply OnLodFixSelected();
	FReply OnLodFixAll();                   // every applicable fix in the tab
	FReply OnLodExport();
	// Writes an optimised *duplicate* of the finding's texture (original left
	// untouched), applying the server's recommended max-size / compression.
	// Returns false + fills OutError on failure; OutNewPath = new asset path.
	bool   ApplyLodFixDuplicate(const FShintLodFinding& Finding,
	                            FString& OutNewPath, FString& OutError);
	// [LOD-STRIP-END]

	// ── HTTP callbacks ────────────────────────────────────────────────────────
	void OnHealthCheckComplete(const FShintRequestResult& Result);
	void OnProjectValidateComplete(const FShintValidateResult& Result);
	void OnBlueprintValidateComplete(const FShintValidateResult& Result);
	void OnBlueprintNamingScanComplete(const FShintValidateResult& Result); // asset-scan chain: naming only
	void OnCodeFixComplete(const FShintFixResult& Result, uint32 FixGeneration);
	// [DASH-STRIP-BEGIN]
	void OnCodeDashboardComplete(const FShintWebDashboardResult& Result);
	// [DASH-STRIP-END]
	void OnAssetScanComplete(const FShintAssetScanResult& Result);
	/** Asset scan triggered by Scan Blueprints — auto-filters to Blueprints, no BP-naming chain. */
	void OnAssetScanFromBPComplete(const FShintAssetScanResult& Result);
	void OnAssetFixComplete(const FShintAssetFixResult& Result);
	// [DASH-STRIP-BEGIN]
	void OnAssetDashboardComplete(const FShintWebDashboardResult& Result);
	// [DASH-STRIP-END]

	// ── UI state helpers ──────────────────────────────────────────────────────
	void SetStatus(ECoreStatus S);
	void SetCodeState(EModuleState S);
	void SetAssetState(EModuleState S);
	void PopulateCodeIssueList(const FShintValidateResult& Result, bool bIsBPScan);
	void PopulateAssetIssueList(const FShintAssetScanResult& Result);
	void ApplyCodeFilter();
	void ApplyAssetFilter();
	void RefreshCodeStats();
	void RefreshAssetStats();

	// Slice B — Quality Score
	void RefreshQualityScore();                               // updates score badge + breakdown from LastQualityScore
	void OnLatestScoreFetched(const FShintQualityScoreSnapshot& Snap);  // /metrics/score/latest callback
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

	// ── State ─────────────────────────────────────────────────────────────────
	TSharedPtr<FShintCoreClient>     CoreClient;
	// Created lazily in Construct after CoreClient. Borrows the
	// transport from CoreClient; lifetime is tied to CoreClient via
	// the shared_ptr -- DashboardSync holds a reference, never null
	// for the panel's lifetime.
	// [DASH-STRIP-BEGIN]
	TSharedPtr<class FShintDashboardSync> DashboardSync;
	// [DASH-STRIP-END]
	TSharedPtr<FCoreProcessManager> ProcessManager;

	ECoreStatus  StatusState = ECoreStatus::Unknown;
	EModuleState CodeState   = EModuleState::Idle;
	EModuleState AssetState  = EModuleState::Idle;

	FShintValidateResult  LastCodeResult;
	FShintAssetScanResult LastAssetResult;
	// [LOD-STRIP-BEGIN]
	FShintLodAuditResult  LastLodResult;       // LOD Auditor (Studio)
	// [LOD-STRIP-END]
	FShintQualityScoreSnapshot LastQualityScore;  // Slice B

	// All issues from last scan
	TArray<FShintIssueItemPtr> AllCodeItems;
	TArray<FShintAssetItemPtr> AllAssetItems;
	// [LOD-STRIP-BEGIN]
	TArray<FShintLodFindingPtr> LodFindingItems;    // all findings from last audit
	TArray<FShintLodFindingPtr> LodFilteredItems;   // visible rows (tab + filters)
	// Shared thumbnail renderer pool for the Asset Optimizer table (Stage 2b).
	// Lazily created on first row generation; one pool backs every row's 34px
	// thumbnail so the editor renders real asset previews instead of a swatch.
	TSharedPtr<class FAssetThumbnailPool> LodThumbnailPool;
	// [LOD-STRIP-END]
	// Currently visible (after filter)
	TArray<FShintIssueItemPtr> CodeIssueItems;
	TArray<FShintAssetItemPtr> AssetIssueItems;

	// [LOD-STRIP-BEGIN]
	// LOD Auditor / Asset Optimizer UI state
	EModuleState LodState        = EModuleState::Idle;
	bool         bLodExplainTop  = false;     // "Explain top issues" toggle
	FString      LodProfile      = TEXT("default"); // "default" | "mobile"
	ELodTab      LodActiveTab    = ELodTab::Textures;
	FString      LodSearchText;
	FString      LodGroupFilter    = TEXT("All Groups");
	FString      LodFormatFilter   = TEXT("All Formats");
	FString      LodSeverityFilter = TEXT("All Severities");
	// [LOD-STRIP-END]

	EIssueFilter         CurrentFilter            = EIssueFilter::All;
	EIssueCategoryFilter CurrentCategoryFilter    = EIssueCategoryFilter::All;
	EIssueSeverityFilter CurrentSeverityFilter    = EIssueSeverityFilter::All;
	EAssetTypeFilter     CurrentAssetTypeFilter   = EAssetTypeFilter::All;
	ECodeTypeFilter      CurrentCodeTypeFilter    = ECodeTypeFilter::All;
	// Toolbar text search (Asset Optimizer design language, replicated on the
	// Code Validator + Naming Bot toolbars). Matched case-insensitively
	// against message/file/rule (code) and names/path (assets).
	FString              CodeSearchText;
	FString              AssetSearchText;

	// Fingerprints "FilePath:Line:RuleId" of issues fixed this session.
	// Prevents re-showing the same issue on an incremental/BP re-scan.
	// Cleared when the user starts a fresh full scan (Scan Project).
	TSet<FString>           AppliedFixFingerprints;
	TArray<FShintCodeIssue> PendingCodeFixes;

	// Incremented every time a new scan starts.
	// The async UBT callback captures the generation at fix-time; if it changed
	// by the time the callback fires, the scan already superseded the build check
	// and BUILD001 errors must not be injected into the new results.
	uint32 ScanGeneration      = 0;
	/** True while the last code scan was a Blueprint-only scan (set by OnScanBlueprintsClicked,
	 *  cleared by OnScanProjectClicked). Used to replace — not merge — the code list. */
	bool   bBlueprintScanActive = false;

	// ── Slate refs ────────────────────────────────────────────────────────────
	TSharedPtr<SListView<FShintIssueItemPtr>> CodeIssueListView;
	TSharedPtr<SListView<FShintAssetItemPtr>> AssetIssueListView;
	// [LOD-STRIP-BEGIN]
	TSharedPtr<SListView<FShintLodFindingPtr>> LodFindingListView;

	// KPI tiles (Asset Optimizer): value + colored breakdown subtitle.
	TSharedPtr<STextBlock> LodFiles_Label;       // FILES — total audited
	TSharedPtr<STextBlock> LodFilesSub_Label;    //   "Textures: N  Meshes: N"
	TSharedPtr<STextBlock> LodMemImpact_Label;   // MEMORY IMPACT — total VRAM
	TSharedPtr<STextBlock> LodMemSavings_Label;  // MEMORY SAVINGS — MB
	TSharedPtr<STextBlock> LodSavingsPct_Label;  //   "37.8% Reduction"
	TSharedPtr<STextBlock> LodFrameTime_Label;   // FRAME TIME SAVINGS (stub)
	TSharedPtr<STextBlock> LodIssues_Label;      // ISSUES — total
	TSharedPtr<STextBlock> LodIssuesSub_Label;   //   "Textures: N  Meshes: N"
	TSharedPtr<STextBlock> LodFixSelected_Label; // bulk "Fix (N)" (checked rows)
	TSharedPtr<STextBlock> LodFixAll_Label;      // "Fix All (N)" (applicable rows)
	// Retained for back-compat with older stat refs (unused by the new layout).
	TSharedPtr<STextBlock> LodAudited_Label;
	TSharedPtr<STextBlock> LodVramSaved_Label;
	TSharedPtr<SButton>    AuditLodBtn;
	TSharedPtr<STextBlock> AuditLodBtnLabel;
	TSharedPtr<SWidget>    LodEmptyState;
	TSharedPtr<class SBox> LodTableHeaderBox;  // per-tab column header host
	// [LOD-STRIP-END]

	TSharedPtr<STextBlock> CodeFiles_Label;
	TSharedPtr<STextBlock> CodeErrors_Label;
	TSharedPtr<STextBlock> CodeWarnings_Label;
	TSharedPtr<STextBlock> CodeScore_Label;            // Slice B — overall Quality Score badge
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

	// Config field widgets. ApiKeyDashboard is the per-project credential the
	// plugin attaches to dashboard requests; ApiKeyMongo is managed by the
	// sign-in flow. The API key identifies the project implicitly server-side.
	TSharedPtr<SEditableTextBox> ApiKeyDashboardField;
	TSharedPtr<SEditableTextBox> DashboardUrlField;
	// Added to mirror the Unity Settings tab layout (Core Engine port, API
	// Key, Excluded Paths, Export Path). All persist back into
	// shinttools.config.json via SaveConfigOverrides; ExcludedPaths feeds
	// CollectSourceFiles-side filtering in ValidateProject.
	TSharedPtr<SEditableTextBox> CorePortField;
	TSharedPtr<SEditableTextBox> ApiKeyMongoField;
	TSharedPtr<class SMultiLineEditableTextBox> ExcludedPathsField;
	TSharedPtr<SEditableTextBox> ExportPathField;

	// ── UI-REDESIGN: navigation state ────────────────────────────────────────
	// The panel routes between four destinations via a SWidgetSwitcher driven
	// by the sidebar. CurrentDestinationIndex is the source of truth; both
	// SShintSidebar and the switcher consume it. Stored as a plain int32 to
	// avoid pulling SShintSidebar.h into this header just to use its enum.
	// The .cpp casts to / from EShintDestination at the boundary.
	int32                             CurrentDestinationIndex = 0; // Overview
	int32                             CurrentConnStateIndex   = 0; // Unknown
	TSharedPtr<class SWidgetSwitcher> DestinationSwitcher;

	/** Switches the active destination. Index matches EShintDestination
	 *  (0 Overview / 1 Code / 2 Assets / 3 Settings). */
	void SetDestinationIndex(int32 Index);
};
