// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"

#include "ShintStyle.h"
#include "SShintCard.h"
#include "ShintIconStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#include "Styling/AppStyle.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace ST4
{
	namespace
	{

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

TSharedRef<SWidget> ShintBtnContent(
	const FName& Icon, const TSharedRef<SWidget>& Label, const FSlateColor& Tint)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		  .Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SImage)
			.Image(FShintIconStyle::GetBrush(Icon))
			.ColorAndOpacity(Tint)
			.DesiredSizeOverride(FVector2D(13.f, 13.f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Label
		];
}

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
		else continue;

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
		GConfig->Flush(false, IniPath);
	}
	return Added;
}

TSharedRef<SWidget> SShintToolsPanel::Divider()
{
	return SNew(SBox).HeightOverride(1.f)
		[ SNew(SBorder).BorderImage(ST4::Solid(C_Border())).Padding(0.f) ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildSectionTitle(const FText& Title, const FText& Subtitle)
{

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
