// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Styling/CoreStyle.h"
#include "Fonts/SlateFontInfo.h"

class FShintStyle
{
public:

	struct Colors
	{
		static FLinearColor Bg()             { return FromHex(0x000000); }
		static FLinearColor BgCard()         { return FromHex(0x161616); }
		static FLinearColor BgCardHover()    { return FromHex(0x202020); }
		static FLinearColor BgTopbar()       { return FromHex(0x0d0d0d); }
		static FLinearColor BgSidebar()      { return FromHex(0x000000); }

		static FLinearColor BorderSubtle()   { return FromHex(0x2a2a2a); }
		static FLinearColor BorderStrong()   { return FromHex(0x3a3a3a); }

		static FLinearColor TextPrimary()    { return FromHex(0xffffff); }
		static FLinearColor TextMuted()      { return FromHex(0x999999); }
		static FLinearColor TextFaint()      { return FromHex(0x3a3a3a); }

		static FLinearColor SevCritical()    { return FromHex(0xef4444); }
		static FLinearColor SevHigh()        { return FromHex(0xf97316); }
		static FLinearColor SevMedium()      { return FromHex(0xa1a1aa); }
		static FLinearColor SevLow()         { return FromHex(0x22c55e); }

		static FLinearColor Success()        { return FromHex(0x22c55e); }
		static FLinearColor Warning()        { return FromHex(0xf97316); }
		static FLinearColor Error()          { return FromHex(0xef4444); }
		static FLinearColor AccentBlue()     { return FromHex(0x3b82f6); }
		static FLinearColor AccentBlueDim()  { return FromHex(0x2563eb); }

		static FLinearColor FromSeverity(const FString& Severity)
		{
			const FString S = Severity.ToLower();
			if (S == TEXT("critical") || S == TEXT("error"))   return SevCritical();
			if (S == TEXT("high")     || S == TEXT("warning")) return SevHigh();
			if (S == TEXT("low")      || S == TEXT("info"))    return SevLow();
			return SevMedium();
		}

	private:

		static FLinearColor FromHex(uint32 RGB)
		{
			const uint8 R = (RGB >> 16) & 0xff;
			const uint8 G = (RGB >>  8) & 0xff;
			const uint8 B =  RGB        & 0xff;
			return FLinearColor(FColor(R, G, B, 0xff));
		}
	};

	struct Space
	{
		static constexpr float S1 =  4.f;
		static constexpr float S2 =  8.f;
		static constexpr float S3 = 12.f;
		static constexpr float S4 = 16.f;
		static constexpr float S5 = 24.f;
		static constexpr float S6 = 32.f;
	};

	struct Radius
	{
		static constexpr float Control =  4.f;
		static constexpr float Card    =  8.f;
		static constexpr float Modal   = 12.f;
	};

	struct Fonts
	{

		static FSlateFontInfo H1()      { return Make(24); }

		static FSlateFontInfo H2()      { return Make(16); }

		static FSlateFontInfo Body()    { return Make(14); }

		static FSlateFontInfo Small()   { return Make(12); }

		static FSlateFontInfo Caption() { return Make(10); }

	private:

		static const FString& BahnschriftPath()
		{
			static const FString Cached = []()
			{
				const FString Path = TEXT("C:/Windows/Fonts/bahnschrift.ttf");
				return FPaths::FileExists(Path) ? Path : FString();
			}();
			return Cached;
		}

		static FSlateFontInfo Make(int32 Size)
		{
			const FString& Path = BahnschriftPath();
			if (!Path.IsEmpty())
			{

				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				return FSlateFontInfo(Path, Size);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}

			return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
		}
	};
};
