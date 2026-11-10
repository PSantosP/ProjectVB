// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Settings/VBCulture.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Kismet/KismetInternationalizationLibrary.h"

namespace
{
	FString NativeCulture()
	{
		return FTextLocalizationManager::Get().GetNativeCultureName(ELocalizedTextSourceCategory::Game);
	}

	TArray<FString> SelectableNames()
	{
		TArray<FString> Names = FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Game);
		const FString Native = NativeCulture();
		Names.Sort();
		if (!Native.IsEmpty())
		{
			Names.Remove(Native);
			Names.Insert(Native, 0);
		}

		return Names;
	}

	// "en-US" 처럼 지역이 붙은 이름을 고를 수 있는 이름("en")으로 맞춘다 - 엔진이 번역을 고르는 우선순위와 같은 규칙이다.
	FString Resolve(const FString& Wanted, const TArray<FString>& Names)
	{
		for (const FString& Candidate : FInternationalization::Get().GetPrioritizedCultureNames(Wanted))
		{
			if (Names.Contains(Candidate))
			{
				return Candidate;
			}
		}

		return FString();
	}

	FString ActiveRaw()
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			const FString Preview = FTextLocalizationManager::Get().GetConfiguredGameLocalizationPreviewLanguage();
			return Preview.IsEmpty() ? NativeCulture() : Preview;
		}
#endif
		return FInternationalization::Get().GetCurrentLanguage()->GetName();
	}
}

namespace VBCulture
{
	FString GetActive()
	{
		const FString Raw = ActiveRaw();
		const FString Resolved = Resolve(Raw, SelectableNames());
		return Resolved.IsEmpty() ? Raw : Resolved;
	}

	FString GetDefault()
	{
		const FString Resolved = Resolve(FInternationalization::Get().GetDefaultLanguage()->GetName(), SelectableNames());
		return Resolved.IsEmpty() ? NativeCulture() : Resolved;
	}

	TArray<FVBCultureChoice> GetSelectable()
	{
		TArray<FVBCultureChoice> Choices;
		for (const FString& Name : SelectableNames())
		{
			const FCulturePtr Culture = FInternationalization::Get().GetCulture(Name);
			Choices.Add({ Name, FText::AsCultureInvariant(Culture ? Culture->GetNativeName() : Name) });
		}

		return Choices;
	}

	void Apply(const FString& Culture)
	{
#if WITH_EDITOR
		// 에디터에서 문화권을 바꾸면 에디터 UI 언어가 바뀐다. PIE 의 게임 글은 미리 보기 언어가 맡는다 -
		//   설정에 남겨 다음 PIE 도 이 언어로 시작하고(BeginPIE 가 읽는다), 지금 PIE 에는 바로 건다.
		if (GIsEditor)
		{
			FTextLocalizationManager::Get().ConfigureGameLocalizationPreviewLanguage(Culture);
			FTextLocalizationManager::Get().EnableGameLocalizationPreview(Culture);
			return;
		}
#endif
		UKismetInternationalizationLibrary::SetCurrentCulture(Culture, /*SaveToConfig=*/true);
	}
}
