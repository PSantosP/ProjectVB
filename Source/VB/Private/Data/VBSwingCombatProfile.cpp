// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBSwingCombatProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
#define LOCTEXT_NAMESPACE "VBSwingCombatProfile"
#endif

bool FVBSwingCombatScales::HasAnyEffect() const
{
	// 정확 비교가 아니라 근사 비교를 쓰는 이유: 에디터에서 1.0 을 넣었다 지웠다 하면 1.0000001 같은 값이
	//  남을 수 있고, 그런 값은 저작 의도상 "안 건드림"이다. 실제로 곱해도 결과가 사실상 같다.
	return !FMath::IsNearlyEqual(DamageScale, 1.0f)
		|| !FMath::IsNearlyEqual(KnockbackStrengthScale, 1.0f)
		|| !FMath::IsNearlyEqual(HitStopDurationScale, 1.0f)
		|| !FMath::IsNearlyEqual(PoiseDamageScale, 1.0f)
		|| !FMath::IsNearlyEqual(TraceRadiusScale, 1.0f);
}

float UVBSwingCombatProfile::ResolveLaunchHeight(const UVBSwingCombatProfile* Profile, float AttackLaunchHeight)
{
	return Profile && Profile->LaunchHeightOverride >= 0.0f ? Profile->LaunchHeightOverride : AttackLaunchHeight;
}

FGameplayTag UVBSwingCombatProfile::ResolveImpactCue(const UVBSwingCombatProfile* Profile, const FGameplayTag& BaseCue)
{
	if (!Profile || !Profile->bHeavyImpact || !BaseCue.IsValid())
	{
		return BaseCue;
	}
	// 강타판은 같은 큐의 .Heavy 자식이라는 약속이고, 없으면 원래 큐를 쓴다
	// 캐시하지 않는다 - 맞은 대상마다 문자열 하나와 태그 조회 하나라 비용이 작고, 캐시는 무효화를 관리할 곳을 하나 더 만든다
	const FGameplayTag Heavy = FGameplayTag::RequestGameplayTag(FName(*(BaseCue.ToString() + TEXT(".Heavy"))), /*ErrorIfNotFound=*/false);
	return Heavy.IsValid() ? Heavy : BaseCue;
}

#if WITH_EDITOR
EDataValidationResult UVBSwingCombatProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 편집기 메타데이터를 우회한 비정상 배율도 저장 전에 거부한다.
	const auto IsBadScale = [](float Scale) { return !FMath::IsFinite(Scale) || Scale < 0.0f; };
	if (IsBadScale(Scales.DamageScale) || IsBadScale(Scales.KnockbackStrengthScale)
		|| IsBadScale(Scales.HitStopDurationScale) || IsBadScale(Scales.PoiseDamageScale)
		|| IsBadScale(Scales.TraceRadiusScale) || Scales.TraceRadiusScale < 0.1f)
	{
		Context.AddError(LOCTEXT("VBSwingCombat_BadScale",
			"배율은 유한한 0 이상의 값이어야 하며 판정 폭 배율은 0.1 이상이어야 합니다."));
		Result = EDataValidationResult::Invalid;
	}

	if (!FMath::IsFinite(Scales.KnockbackZScale) || !FMath::IsNearlyEqual(Scales.KnockbackZScale, 1.0f))
	{
		Context.AddError(LOCTEXT("LegacyPopScale", "KnockbackZScale은 폐기된 상방 밀치기 배율입니다. 1로 설정하고 띄우기는 LaunchHeightOverride를 사용하세요."));
		Result = EDataValidationResult::Invalid;
	}

	// 배율이 모두 항등원이고 등급, 화면 연출도 없으면 노티에 꽂혀도 아무 일도 안 해 배선 실패와 구별이 안 된다
	// 등급 덮어쓰기만 있거나 화면 연출만 있는 자산은 의도한 저작이라 유효하다
	if (!Scales.HasAnyEffect() && !bOverrideHitImpact && LaunchHeightOverride < 0.0f && !ScreenFeedback && !bHeavyImpact)
	{
		Context.AddError(LOCTEXT("VBSwingCombat_NoEffect",
			"This profile has identity scales and no impact, launch-height or screen-feedback override. "
			"It has no authored effect, which is indistinguishable from missing notify wiring. "
			"Author an override or remove the unused profile from the notify."));

		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
