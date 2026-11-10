// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitDistortionConfig.h"

#include "NiagaraSystem.h"
#include "Types/VBNiagaraParameters.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FName UVBHitDistortionConfig::StrengthParameter(TEXT("User.Strength"));
const FName UVBHitDistortionConfig::RadiusParameter(TEXT("User.RadiusCm"));
const FName UVBHitDistortionConfig::LifetimeParameter(TEXT("User.Lifetime"));
const FName UVBHitDistortionConfig::PeakTimeParameter(TEXT("User.PeakTime"));
const FName UVBHitDistortionConfig::DirectionParameter(TEXT("User.Direction"));

const FVBHitDistortionProfile& UVBHitDistortionConfig::ResolveProfile(EVBHitImpact Impact) const
{
	const FVBHitDistortionProfile* Found = ByImpact.Find(Impact);
	return Found ? *Found : DefaultProfile;
}

bool UVBHitDistortionConfig::ValidateProfile(const FVBHitDistortionProfile& Profile, FString& OutError)
{
	if (!Profile.System)
	{
		return true; // 비운 프로필 = 이 등급은 끔
	}

	if (!(Profile.Strength > 0.0f && Profile.Strength <= 0.1f))
	{
		OutError = FString::Printf(TEXT("Strength %.4f must be in (0, 0.1]"), Profile.Strength);
		return false;
	}

	if (!(Profile.RadiusCm > 0.0f))
	{
		OutError = FString::Printf(TEXT("RadiusCm %.1f must be > 0"), Profile.RadiusCm);
		return false;
	}

	if (!(Profile.PeakTime > 0.0f && Profile.PeakTime < Profile.Lifetime && Profile.Lifetime <= 0.5f))
	{
		OutError = FString::Printf(TEXT("needs 0 < PeakTime (%.3f) < Lifetime (%.3f) <= 0.5"), Profile.PeakTime, Profile.Lifetime);
		return false;
	}

	for (const FName Parameter : { StrengthParameter, RadiusParameter, LifetimeParameter, PeakTimeParameter, DirectionParameter })
	{
		// 없는 이름에 넣은 값은 Niagara 가 말없이 버린다 - 왜곡이 기본값으로 나와도 아무도 모른다.
		if (!VBNiagaraParameters::HasUserFloat(*Profile.System, Parameter))
		{
			OutError = FString::Printf(TEXT("%s has no float %s"), *Profile.System->GetName(), *Parameter.ToString());
			return false;
		}
	}

	return true;
}

#if WITH_EDITOR
EDataValidationResult UVBHitDistortionConfig::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = Super::IsDataValid(Context) != EDataValidationResult::Invalid;
	auto Check = [&](const FVBHitDistortionProfile& Profile, const FString& Name)
	{
		FString Error;
		if (!ValidateProfile(Profile, Error))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("%s: %s"), *Name, *Error)));
			bValid = false;
		}
	};
	Check(DefaultProfile, TEXT("DefaultProfile"));
	for (const TPair<EVBHitImpact, FVBHitDistortionProfile>& Pair : ByImpact)
	{
		Check(Pair.Value, VBHitImpactName(Pair.Key));
	}

	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
