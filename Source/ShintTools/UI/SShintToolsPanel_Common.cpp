// Copyright ShintTools. All Rights Reserved.
//
// Shared infrastructure for the SShintToolsPanel translation-unit family:
//   * ST4::Solid / ST4::Outline brush cache (process-lifetime TMap)
//   * ShintShowErrorToast            — uniform CS_Fail notification
//   * StatBadge                      — KPI tile factory used by Code + Asset sections
//   * CoreRedirects writer           — DefaultEngine.ini patcher for asset renames
//   * Static SShintToolsPanel helpers — Divider, BuildSectionTitle, BuildDiffLine,
//                                       BuildContextPanel, BuildModuleProgressBar, FmtN
//
// None of these reach into per-panel state. They are split out so the body
// of the panel can grow without forcing every TU to recompile a single
// 3.5k-line file.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools/ShintTools.h"

// Shared design-system widgets
#include "ShintStyle.h"
#include "SShintCard.h"

// Slate layout / widgets
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

// Style
#include "Styling/AppStyle.h"

// Config / paths
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

// ─────────────────────────────────────────────────────────────────────────────
// Brush cache
// ─────────────────────────────────────────────────────────────────────────────
namespace ST4
{
	namespace
	{
		// Function-local static — single process-lifetime cache shared by every
		// panel TU. Returning a reference keeps the brush pointers stable for
		// Slate to retain across paint ticks.
		TMap<FString, TUniquePtr<FSlateBrush>>& Cache()
		{
			static TMap<FString, TUniquePtr<FSlateBrush>> BC;
			return BC;
		}
	}

	const FSlateBrush* Solid(const FLinearColor& C, float R)
	{
		const FString K = FString::Printf(TEXT("S%.3f%.3f%.3f%.1f"), C.R, C.G, C.B, R);
		TMap<FString, TUniquePtr<FSlateBrush>>& BC = Cache();
		if (!BC.Contains(K))
		{
			TUniquePtr<FSlateBrush> B = MakeUnique<FSlateBrush>();
			B->TintColor = FSlateColor(C);
			B->DrawAs    = R > 0.f ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::Box;
			if (R > 0.f)
			{
				B->OutlineSettings.CornerRadii  = FVector4(R, R, R, R);
				B->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			BC.Add(K, MoveTemp(B));
		}
		return BC[K].Get();
	}

	const FSlateBrush* Outline(const FLinearColor& Fill, const FLinearColor& Brd, float R)
	{
		const FString K = FString::Printf(TEXT("O%.3f%.3f%.1f"), Fill.R, Brd.R, R);
		TMap<FString, TUniquePtr<FSlateBrush>>& BC = Cache();
		if (!BC.Contains(K))
		{
			TUniquePtr<FSlateBrush> B = MakeUnique<FSlateBrush>();
			B->TintColor = FSlateColor(Fill);
			B->DrawAs    = ESlateBrushDrawType::RoundedBox;
			B->OutlineSettings.CornerRadii  = FVector4(R, R, R, R);
			B->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			B->OutlineSettings.Color        = FSlateColor(Brd);
			B->OutlineSettings.Width        = 1.f;
			BC.Add(K, MoveTemp(B));
		}
		return BC[K].Get();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Toast
// ─────────────────────────────────────────────────────────────────────────────
void ShintShowErrorToast(const FString& Title, const FString& Detail)
{
	FNotificationInfo Info(FText::FromString(Title));
	Info.SubText      = FText::FromString(Detail.IsEmpty()
		? FString(TEXT("Check that the Core Engine container is running and that "
		               "core_host / core_port in shinttools.config.json point to it."))
		: Detail);
	Info.ExpireDuration = 8.0f;
	Info.bUseLargeFont  = false;
	Info.bUseSuccessFailIcons = true;
	TSharedPtr<SNotificationItem> N = FSlateNotificationManager::Get().AddNotification(Info);
	if (N.IsValid()) N->SetCompletionState(SNotificationItem::CS_Fail);
	UE_LOG(LogShintTools, Error, TEXT("%s — %s"), *Title, *Detail);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stat badge
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> StatBadge(
	TSharedPtr<STextBlock>& OutLabel, const FText& Caption, const FLinearColor& Clr)
{
	SAssignNew(OutLabel, STextBlock)
		.Text(FText::FromString(TEXT("—")))
		.Font(FShintStyle::Fonts::H1())
		.ColorAndOpacity(FSlateColor(Clr));

	return SNew(SShintCard)
		.bShowHeader(false)
		.ContentPadding(FShintStyle::Space::S3)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S1))
			[
				SNew(STextBlock)
				.Text(Caption)
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				OutLabel.ToSharedRef()
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// CoreRedirects writer.
//
// IAssetTools::RenameAssets + FixupReferencers fixes references that exist as
// soft/hard pointers in other LOADED assets. It does NOT survive when the
// .uasset redirector is later deleted, and it does not cover unloaded packages
// or **native parent class references** — child Blueprints whose parent is a
// renamed Blueprint reload with "Class not found" until you provide a
// CoreRedirects mapping.
//
// Three redirect kinds per rename, emitted from the asset-fix flow:
//   +ClassRedirects   — Blueprint generated-class lookup ("BP_Old_C")
//   +PackageRedirects — soft package paths ("/Game/.../BP_Old")
//   +ObjectRedirects  — UObject paths ("/Game/.../BP_Old.BP_Old")
//
// Existing entries are deduplicated so re-running the bot is idempotent.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	FString FormatCoreRedirectValue(const FString& OldName, const FString& NewName)
	{
		return FString::Printf(TEXT("(OldName=\"%s\",NewName=\"%s\")"), *OldName, *NewName);
	}
}

int32 WriteShintCoreRedirects(const TArray<FShintRedirectEntry>& Entries)
{
	if (Entries.IsEmpty()) return 0;

	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
	const TCHAR* Section  = TEXT("CoreRedirects");

	// Read existing entries so we can dedupe. GConfig stores +Foo=... lines as
	// an array under the Foo key, so we split read by entry kind.
	TArray<FString> ExistingClass;
	TArray<FString> ExistingPackage;
	TArray<FString> ExistingObject;
	GConfig->GetArray(Section, TEXT("+ClassRedirects"),   ExistingClass,   IniPath);
	GConfig->GetArray(Section, TEXT("+PackageRedirects"), ExistingPackage, IniPath);
	GConfig->GetArray(Section, TEXT("+ObjectRedirects"),  ExistingObject,  IniPath);

	int32 Added = 0;
	for (const FShintRedirectEntry& E : Entries)
	{
		const FString Value = FormatCoreRedirectValue(E.OldName, E.NewName);

		TArray<FString>* Bucket = nullptr;
		if      (E.Key.Equals(TEXT("+ClassRedirects")))   Bucket = &ExistingClass;
		else if (E.Key.Equals(TEXT("+PackageRedirects"))) Bucket = &ExistingPackage;
		else if (E.Key.Equals(TEXT("+ObjectRedirects")))  Bucket = &ExistingObject;
		else continue; // unknown redirect kind — skip rather than corrupt the .ini

		const bool bAlreadyPresent = Bucket->ContainsByPredicate(
			[&Value](const FString& S) { return S.Equals(Value, ESearchCase::IgnoreCase); });
		if (!bAlreadyPresent)
		{
			Bucket->Add(Value);
			++Added;
		}
	}

	if (Added > 0)
	{
		GConfig->SetArray(Section, TEXT("+ClassRedirects"),   ExistingClass,   IniPath);
		GConfig->SetArray(Section, TEXT("+PackageRedirects"), ExistingPackage, IniPath);
		GConfig->SetArray(Section, TEXT("+ObjectRedirects"),  ExistingObject,  IniPath);
		GConfig->Flush(/*Read=*/false, IniPath);
	}
	return Added;
}

// ─────────────────────────────────────────────────────────────────────────────
// SShintToolsPanel static helpers
//
// These are class statics declared on the panel; the original .cpp kept them
// inline. They are stateless and only depend on the panel's brand-palette /
// font accessors, so they live here instead of forcing every TU to depend on
// the panel translation unit just to call ::Divider() or ::BuildSectionTitle().
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::Divider()
{
	return SNew(SBox).HeightOverride(1.f)
		[ SNew(SBorder).BorderImage(ST4::Solid(C_Border())).Padding(0.f) ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildSectionTitle(const FText& Title, const FText& Subtitle)
{
	// Uses ShintStyle tokens so all section headings render with the same
	// Bahnschrift typography + spacing as the rest of the dashboard.
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S1)
		[
			SNew(STextBlock).Text(Title)
			.Font(FShintStyle::Fonts::H2())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S4)
		[
			SNew(STextBlock).Text(Subtitle)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildDiffLine(
	const FString& Icon, const FString& Text,
	const FLinearColor& IconColor, const FLinearColor& TextColor)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock).Text(FText::FromString(Icon)).Font(F_Mono())
			.ColorAndOpacity(FSlateColor(IconColor))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(STextBlock).Text(FText::FromString(Text)).Font(F_Mono())
			.ColorAndOpacity(FSlateColor(TextColor)).AutoWrapText(true)
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildContextPanel(
	const FString& Label, const FString& ContextText,
	int32 ContextLineStart, int32 IssueLineNo,
	const FLinearColor& HighlightColor)
{
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);

	Lines->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock).Text(FText::FromString(Label)).Font(F_Label())
		.ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TArray<FString> SrcLines;
	ContextText.ParseIntoArray(SrcLines, TEXT("\n"), false);

	for (int32 Idx = 0; Idx < SrcLines.Num(); ++Idx)
	{
		const int32 LineNo = ContextLineStart + Idx;
		const bool  bIsIssueLine = (LineNo == IssueLineNo);
		const FLinearColor TextCol = bIsIssueLine ? HighlightColor : C_Gray();
		const FString Prefix = FString::Printf(TEXT("%4d  "), LineNo);

		Lines->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(FText::FromString(Prefix))
				.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Text(FText::FromString(SrcLines[Idx]))
				.Font(F_Mono()).ColorAndOpacity(FSlateColor(TextCol))
				.AutoWrapText(false)
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_CodeBG()))
		.Padding(FMargin(8.f, 6.f))
		[ Lines ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildModuleProgressBar(
	TSharedPtr<SProgressBar>& OutBar,
	TAttribute<TOptional<float>> PercentAttr)
{
	return SNew(SBox).HeightOverride(2.f)
		[
			SAssignNew(OutBar, SProgressBar)
			.Percent(PercentAttr)
			.FillColorAndOpacity(FSlateColor(C_Blue()))
			.BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
		];
}

FString SShintToolsPanel::FmtN(int32 N)
{
	return N < 0 ? TEXT("—") : FString::Printf(TEXT("%d"), N);
}
