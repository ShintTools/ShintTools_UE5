// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"
#include "CoreProcessManager.h"

#include "ShintStyle.h"
#include "SShintSidebar.h"
#include "SShintTopBar.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"

#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

void SShintToolsPanel::Construct(const FArguments& InArgs)
{
	CoreClient     = MakeShared<FShintCoreClient>();
	ProcessManager = MakeShared<FCoreProcessManager>();

	SetStatus(ECoreStatus::Checking);
	CoreClient->CheckHealth(FOnShintRequestComplete::CreateSP(
		this, &SShintToolsPanel::OnHealthCheckComplete));

	auto CurrentTitle = [this]() -> FText
	{
		switch (static_cast<EShintDestination>(CurrentDestinationIndex))
		{
		case EShintDestination::Code:     return LOCTEXT("TitleCode",     "Code Validator");
		case EShintDestination::Assets:   return LOCTEXT("TitleAssets",   "Asset Naming Bot");
		case EShintDestination::Settings: return LOCTEXT("TitleSettings", "Settings");
		case EShintDestination::Overview:
		default:                          return LOCTEXT("TitleOverview", "Overview");
		}
	};

	auto StatusText = [this]() -> FText
	{
		switch (static_cast<EShintConnState>(CurrentConnStateIndex))
		{
		case EShintConnState::Connected:    return LOCTEXT("Conn",  "Connected");
		case EShintConnState::Connecting:   return LOCTEXT("Probe", "Connecting…");
		case EShintConnState::Disconnected: return LOCTEXT("Down",  "Core offline");
		case EShintConnState::Unknown:
		default:

			return FText::GetEmpty();
		}
	};

	auto ActiveDest = [this]() -> EShintDestination
	{
		return static_cast<EShintDestination>(CurrentDestinationIndex);
	};

	auto ConnState = [this]() -> EShintConnState
	{
		return static_cast<EShintConnState>(CurrentConnStateIndex);
	};

	auto TierText = []() -> FText
	{
		const FString Tier = FShintToolsModule::GetCachedTier();
		if (Tier.IsEmpty())
		{
			return FText::GetEmpty();
		}

		FString Display = Tier.ToLower();
		if (!Display.IsEmpty())
		{
			Display[0] = FChar::ToUpper(Display[0]);
		}
		return FText::FromString(Display);
	};

	auto WrapSection = [](TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
	{
		return SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			.Padding(FMargin(FShintStyle::Space::S5))
			[ Content ];
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SShintTopBar)
				.Title_Lambda(CurrentTitle)
				.StatusText_Lambda(StatusText)
				.TierText_Lambda(TierText)
				.ConnState_Lambda(ConnState)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SShintSidebar)
					.Active_Lambda(ActiveDest)
					.OnSelected_Lambda([this](EShintDestination Dest)
					{
						SetDestinationIndex(static_cast<int32>(Dest));
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SAssignNew(DestinationSwitcher, SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return CurrentDestinationIndex; })

					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildOverviewHero()) ]

					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildCodeValidatorSection()) ]

					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildAssetNamingSection()) ]

					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildConfigSection()) ]
				]
			]
		]
	];
}

void SShintToolsPanel::SetDestinationIndex(int32 Index)
{

	if (Index < 0) Index = 0;
	if (Index > 4) Index = 4;
	CurrentDestinationIndex = Index;

}

SShintToolsPanel::~SShintToolsPanel()
{

}

#undef LOCTEXT_NAMESPACE
